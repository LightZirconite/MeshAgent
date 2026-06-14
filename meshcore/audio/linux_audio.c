/*
Copyright 2024 Intel Corporation

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

/*
 * Linux PulseAudio capture for MeshAgent desktop audio streaming.
 *
 * Two independent sources (best-effort on Linux):
 *   - AUDIO_SOURCE_SYSTEM : monitor of the default sink (what plays on the PC)
 *   - AUDIO_SOURCE_MIC    : the default capture source (microphone)
 *
 * PulseAudio is loaded at runtime via dlopen — no compile-time dependency.
 * Capture is requested as PCM S16LE 48000Hz stereo; the agent then decimates
 * and/or downmixes to mono according to the browser-controlled quality level.
 */

#ifndef WIN32

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <dlfcn.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "audio_relay.h"
#include "meshcore/meshdefines.h"

/* ---- PulseAudio simple API (dlopen, no compile-time dependency) ---- */

#define PA_STREAM_RECORD    2
#define PA_SAMPLE_S16LE     3

typedef struct {
    int      format;
    uint32_t rate;
    uint8_t  channels;
} pa_sample_spec_t;

typedef struct {
    uint32_t maxlength;
    uint32_t tlength;
    uint32_t prebuf;
    uint32_t minreq;
    uint32_t fragsize;
} pa_buffer_attr_t;

typedef void pa_simple_t;

typedef pa_simple_t* (*pa_simple_new_fn)(
    const char *server, const char *name, int dir, const char *dev,
    const char *stream_name, const pa_sample_spec_t *ss,
    const void *map, const void *attr, int *error);
typedef int  (*pa_simple_read_fn)(pa_simple_t *s, void *data, size_t bytes, int *error);
typedef void (*pa_simple_free_fn)(pa_simple_t *s);

static pa_simple_new_fn  g_pa_new  = NULL;
static pa_simple_read_fn g_pa_read = NULL;
static pa_simple_free_fn g_pa_free = NULL;
static void             *g_pa_lib  = NULL;

/* ---- Per-source state ---- */

typedef struct AudioSource
{
    int               sourceId;
    pthread_t         tid;
    volatile int      started;
    volatile int      shutdown;
    volatile int      level;
} AudioSource;

static AudioSource        g_src[AUDIO_SOURCE_COUNT];
static AUDIO_WriteHandler g_write_handler = NULL;
static void              *g_reserved      = NULL;

/* ---- Capture format ---- */

#define CAPTURE_RATE     48000
#define CAPTURE_CHANNELS 2
#define CHUNK_FRAMES     960   /* 20ms at 48kHz (divisible by 1,2,3,6) */

/* ---- Helpers ---- */

static short clamp_s16(long long v)
{
    if (v >  32767) return  32767;
    if (v < -32768) return -32768;
    return (short)v;
}

static void level_to_params(int level, int *factor, int *mono)
{
    switch (level)
    {
    case 0:  *factor = 1; *mono = 0; break;
    case 1:  *factor = 1; *mono = 1; break;
    case 2:  *factor = 2; *mono = 1; break;
    case 3:  *factor = 3; *mono = 1; break;
    default: *factor = 6; *mono = 1; break;
    }
}

static void send_audio_info(int sourceId, uint32_t sampleRate, uint16_t channels, uint16_t bitsPerSample)
{
    unsigned char buf[13];
    if (!g_write_handler) return;
    ((uint16_t*)buf)[0] = htons((uint16_t)MNG_AUDIO_INFO);
    ((uint16_t*)buf)[1] = htons(13);
    ((uint32_t*)buf)[1] = htonl(sampleRate);
    ((uint16_t*)buf)[4] = htons(channels);
    ((uint16_t*)buf)[5] = htons(bitsPerSample);
    buf[12] = (unsigned char)sourceId;
    g_write_handler((char*)buf, 13, g_reserved);
}

static void send_audio_data(int sourceId, const short *data_s16, uint32_t nFrames, uint16_t channels, uint16_t seq)
{
    int pcm_bytes, total_size;
    char *pkt;
    if (nFrames == 0 || !g_write_handler) return;
    pcm_bytes  = (int)(nFrames * channels * 2);
    total_size = 8 + pcm_bytes;
    if (total_size > 65535) return;
    pkt = (char*)malloc(total_size);
    if (!pkt) return;

    ((uint16_t*)pkt)[0] = htons((uint16_t)MNG_AUDIO_DATA);
    ((uint16_t*)pkt)[1] = htons((uint16_t)total_size);
    pkt[4] = (char)(unsigned char)sourceId;
    pkt[5] = 0;
    ((uint16_t*)pkt)[3] = htons(seq);
    memcpy(pkt + 8, data_s16, pcm_bytes);
    g_write_handler(pkt, total_size, g_reserved);
    free(pkt);
}

/* ---- Capture thread ---- */

static void* audio_capture_thread(void *arg)
{
    AudioSource     *src = (AudioSource*)arg;
    pa_sample_spec_t ss;
    pa_buffer_attr_t attr;
    pa_simple_t     *stream = NULL;
    int              error  = 0;
    const char      *dev;
    size_t           chunk_bytes = (size_t)(CHUNK_FRAMES * CAPTURE_CHANNELS * sizeof(short));
    short           *inBuf  = (short*)malloc(chunk_bytes);
    short           *outBuf = (short*)malloc(chunk_bytes); /* output never larger than input */
    long long        acc[CAPTURE_CHANNELS];
    int              accN = 0;
    int              curLevel = -1, factor = 1, mono = 0;
    uint16_t         seq = 0;
    int              i;

    for (i = 0; i < CAPTURE_CHANNELS; i++) acc[i] = 0;
    if (!inBuf || !outBuf) { free(inBuf); free(outBuf); return NULL; }

    ss.format   = PA_SAMPLE_S16LE;
    ss.rate     = CAPTURE_RATE;
    ss.channels = CAPTURE_CHANNELS;
    attr.maxlength = (uint32_t)-1;
    attr.tlength   = (uint32_t)-1;
    attr.prebuf    = (uint32_t)-1;
    attr.minreq    = (uint32_t)-1;
    attr.fragsize  = (uint32_t)chunk_bytes;

    /* system -> monitor of default sink; mic -> default capture source */
    dev = (src->sourceId == AUDIO_SOURCE_MIC) ? "@DEFAULT_SOURCE@" : "@DEFAULT_MONITOR@";

    stream = g_pa_new(NULL, "MeshAgent", PA_STREAM_RECORD, dev,
                      (src->sourceId == AUDIO_SOURCE_MIC) ? "Microphone" : "Desktop Audio",
                      &ss, NULL, &attr, &error);
    if (!stream)
    {
        /* Fall back to the default source if the named device is unavailable */
        stream = g_pa_new(NULL, "MeshAgent", PA_STREAM_RECORD, NULL,
                          (src->sourceId == AUDIO_SOURCE_MIC) ? "Microphone" : "Desktop Audio",
                          &ss, NULL, &attr, &error);
    }
    if (!stream) { free(inBuf); free(outBuf); return NULL; }

    while (!src->shutdown)
    {
        int lvl = src->level;
        uint32_t f;
        uint32_t outFrames = 0;
        int effCh;

        if (lvl != curLevel)
        {
            curLevel = lvl;
            level_to_params(curLevel, &factor, &mono);
            for (i = 0; i < CAPTURE_CHANNELS; i++) acc[i] = 0;
            accN = 0;
            send_audio_info(src->sourceId, CAPTURE_RATE / (uint32_t)factor,
                            (uint16_t)(mono ? 1 : CAPTURE_CHANNELS), 16);
        }
        effCh = mono ? 1 : CAPTURE_CHANNELS;

        if (g_pa_read(stream, inBuf, chunk_bytes, &error) < 0) break;
        if (src->shutdown) break;

        for (f = 0; f < CHUNK_FRAMES; f++)
        {
            int ch;
            for (ch = 0; ch < CAPTURE_CHANNELS; ch++)
                acc[ch] += inBuf[f * CAPTURE_CHANNELS + ch];
            if (++accN >= factor)
            {
                if (mono)
                {
                    long long sum = 0;
                    for (ch = 0; ch < CAPTURE_CHANNELS; ch++) { sum += acc[ch]; acc[ch] = 0; }
                    outBuf[outFrames] = clamp_s16(sum / ((long long)factor * CAPTURE_CHANNELS));
                    outFrames++;
                }
                else
                {
                    for (ch = 0; ch < CAPTURE_CHANNELS; ch++)
                    {
                        outBuf[outFrames * CAPTURE_CHANNELS + ch] = clamp_s16(acc[ch] / factor);
                        acc[ch] = 0;
                    }
                    outFrames++;
                }
                accN = 0;
            }
        }
        if (outFrames > 0)
            send_audio_data(src->sourceId, outBuf, outFrames, (uint16_t)effCh, seq++);
    }

    g_pa_free(stream);
    free(inBuf);
    free(outBuf);
    return NULL;
}

/* ---- Public API ---- */

static int source_valid(int sourceId)
{
    return (sourceId >= 0 && sourceId < AUDIO_SOURCE_COUNT);
}

static int load_pulse(void)
{
    if (g_pa_lib) return 1;
    g_pa_lib = dlopen("libpulse-simple.so.0", RTLD_LAZY | RTLD_GLOBAL);
    if (!g_pa_lib) g_pa_lib = dlopen("libpulse-simple.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!g_pa_lib) return 0;

    g_pa_new  = (pa_simple_new_fn) dlsym(g_pa_lib, "pa_simple_new");
    g_pa_read = (pa_simple_read_fn)dlsym(g_pa_lib, "pa_simple_read");
    g_pa_free = (pa_simple_free_fn)dlsym(g_pa_lib, "pa_simple_free");
    if (!g_pa_new || !g_pa_read || !g_pa_free)
    {
        dlclose(g_pa_lib);
        g_pa_lib = NULL;
        return 0;
    }
    return 1;
}

void audio_relay_setup(AUDIO_WriteHandler writeHandler, void *reserved, void *chain, int sourceId, int quality)
{
    AudioSource *src;
    (void)chain;
    if (!source_valid(sourceId)) return;
    if (!load_pulse()) return; /* PulseAudio not installed — skip silently */
    if (quality < 0) quality = 0;
    if (quality > AUDIO_QUALITY_MAX_LEVEL) quality = AUDIO_QUALITY_MAX_LEVEL;

    src = &g_src[sourceId];
    src->sourceId = sourceId;

    g_write_handler = writeHandler;
    g_reserved      = reserved;

    if (src->started)
    {
        src->level = quality; /* already running: act as a quality update */
        return;
    }

    src->level    = quality;
    src->shutdown = 0;
    if (pthread_create(&src->tid, NULL, audio_capture_thread, src) == 0)
        src->started = 1;
}

void audio_relay_set_quality(int sourceId, int level)
{
    if (!source_valid(sourceId)) return;
    if (level < 0) level = 0;
    if (level > AUDIO_QUALITY_MAX_LEVEL) level = AUDIO_QUALITY_MAX_LEVEL;
    g_src[sourceId].level = level;
}

void audio_relay_stop_source(int sourceId)
{
    int i, anyActive;
    AudioSource *src;
    if (!source_valid(sourceId)) return;
    src = &g_src[sourceId];

    src->shutdown = 1;
    if (src->started)
    {
        pthread_join(src->tid, NULL);
        src->started = 0;
    }

    anyActive = 0;
    for (i = 0; i < AUDIO_SOURCE_COUNT; i++)
        if (g_src[i].started) anyActive = 1;
    if (!anyActive)
    {
        g_write_handler = NULL;
        g_reserved      = NULL;
    }
}

void audio_relay_stop(void)
{
    int i;
    for (i = 0; i < AUDIO_SOURCE_COUNT; i++)
    {
        g_src[i].shutdown = 1;
        if (g_src[i].started)
        {
            pthread_join(g_src[i].tid, NULL);
            g_src[i].started = 0;
        }
    }
    g_write_handler = NULL;
    g_reserved      = NULL;
}

#endif /* !WIN32 */
