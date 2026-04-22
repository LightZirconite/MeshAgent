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
 * Linux PulseAudio monitor capture for MeshAgent desktop audio streaming.
 *
 * Captures whatever audio is playing on the default output device
 * (using PulseAudio's default source, which is typically the monitor
 * of the default sink) and streams it as MNG_AUDIO_DATA packets through
 * the KVM write handler.
 *
 * PulseAudio is loaded at runtime via dlopen — no compile-time dependency.
 * Format: PCM S16LE 48000Hz stereo (always, for browser compatibility).
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
    int      format;    /* PA_SAMPLE_S16LE */
    uint32_t rate;
    uint8_t  channels;
} pa_sample_spec_t;

/* pa_simple, pa_channel_map, pa_buffer_attr are all opaque */
typedef void pa_simple_t;

typedef pa_simple_t* (*pa_simple_new_fn)(
    const char *server,
    const char *name,
    int         dir,
    const char *dev,
    const char *stream_name,
    const pa_sample_spec_t *ss,
    const void *map,    /* pa_channel_map*, NULL = default */
    const void *attr,   /* pa_buffer_attr*, NULL = default  */
    int        *error);

typedef int  (*pa_simple_read_fn)(pa_simple_t *s, void *data, size_t bytes, int *error);
typedef void (*pa_simple_free_fn)(pa_simple_t *s);

static pa_simple_new_fn  g_pa_new  = NULL;
static pa_simple_read_fn g_pa_read = NULL;
static pa_simple_free_fn g_pa_free = NULL;
static void             *g_pa_lib  = NULL;

/* ---- Thread state ---- */

static pthread_t          g_audio_tid;
static volatile int       g_audio_started  = 0;
static volatile int       g_audio_shutdown = 0;
static AUDIO_WriteHandler g_write_handler  = NULL;
static void              *g_reserved       = NULL;

/* ---- Packet helpers ---- */

static void send_audio_info(uint32_t sampleRate, uint16_t channels, uint16_t bitsPerSample)
{
    unsigned char buf[12];
    ((uint16_t*)buf)[0] = htons((uint16_t)MNG_AUDIO_INFO);
    ((uint16_t*)buf)[1] = htons(12);
    ((uint32_t*)buf)[1] = htonl(sampleRate);
    ((uint16_t*)buf)[4] = htons(channels);
    ((uint16_t*)buf)[5] = htons(bitsPerSample);
    g_write_handler((char*)buf, 12, g_reserved);
}

static uint32_t timestamp_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000UL + tv.tv_usec / 1000UL);
}

static void send_audio_data(const short *data_s16, uint32_t nFrames, uint16_t channels)
{
    if (nFrames == 0) return;
    int pcm_bytes  = (int)(nFrames * channels * 2); /* 2 bytes/sample S16 */
    int total_size = 8 + pcm_bytes;
    char *pkt = (char*)malloc(total_size);
    if (!pkt) return;

    ((uint16_t*)pkt)[0] = htons((uint16_t)MNG_AUDIO_DATA);
    ((uint16_t*)pkt)[1] = htons((uint16_t)total_size);
    ((uint32_t*)pkt)[1] = htonl(timestamp_ms());
    memcpy(pkt + 8, data_s16, pcm_bytes);
    g_write_handler(pkt, total_size, g_reserved);
    free(pkt);
}

/* ---- PulseAudio capture thread ---- */

#define CAPTURE_RATE     48000
#define CAPTURE_CHANNELS 2
#define CHUNK_FRAMES     2400  /* 50ms at 48kHz */

static void* audio_capture_thread(void *arg)
{
    pa_sample_spec_t ss;
    pa_simple_t     *stream = NULL;
    int              error  = 0;
    size_t chunk_bytes = (size_t)(CHUNK_FRAMES * CAPTURE_CHANNELS * sizeof(short));
    short *buf = (short*)malloc(chunk_bytes);
    if (!buf) return NULL;

    ss.format   = PA_SAMPLE_S16LE;
    ss.rate     = CAPTURE_RATE;
    ss.channels = CAPTURE_CHANNELS;

    /*
     * dev = NULL: use the default source.
     * On most PulseAudio setups the default source IS the monitor of
     * the default output sink. If not, the user can set the default
     * source via `pactl set-default-source <sink>.monitor`.
     */
    stream = g_pa_new(NULL, "MeshAgent", PA_STREAM_RECORD,
                      NULL, "Desktop Audio", &ss, NULL, NULL, &error);
    if (!stream) { free(buf); return NULL; }

    /* Tell the browser the audio format once */
    send_audio_info(CAPTURE_RATE, CAPTURE_CHANNELS, 16);

    /* pa_simple_read blocks until CHUNK_FRAMES are available — clean 50ms bursts */
    while (!g_audio_shutdown)
    {
        if (g_pa_read(stream, buf, chunk_bytes, &error) < 0) break;
        if (!g_audio_shutdown) send_audio_data(buf, CHUNK_FRAMES, CAPTURE_CHANNELS);
    }

    g_pa_free(stream);
    free(buf);
    return NULL;
}

/* ---- Public API ---- */

void audio_relay_setup(AUDIO_WriteHandler writeHandler, void *reserved, void *chain)
{
    if (g_audio_started) return;

    /* Load PulseAudio simple library at runtime — graceful no-op if absent */
    if (!g_pa_lib)
    {
        g_pa_lib = dlopen("libpulse-simple.so.0", RTLD_LAZY | RTLD_GLOBAL);
        if (!g_pa_lib)
            g_pa_lib = dlopen("libpulse-simple.so", RTLD_LAZY | RTLD_GLOBAL);
        if (!g_pa_lib)
            return; /* PulseAudio not installed — skip silently */

        g_pa_new  = (pa_simple_new_fn) dlsym(g_pa_lib, "pa_simple_new");
        g_pa_read = (pa_simple_read_fn)dlsym(g_pa_lib, "pa_simple_read");
        g_pa_free = (pa_simple_free_fn)dlsym(g_pa_lib, "pa_simple_free");

        if (!g_pa_new || !g_pa_read || !g_pa_free)
        {
            dlclose(g_pa_lib);
            g_pa_lib = NULL;
            return;
        }
    }

    g_write_handler  = writeHandler;
    g_reserved       = reserved;
    g_audio_shutdown = 0;

    if (pthread_create(&g_audio_tid, NULL, audio_capture_thread, NULL) == 0)
        g_audio_started = 1;
}

void audio_relay_stop(void)
{
    g_audio_shutdown = 1;
    if (g_audio_started)
    {
        pthread_join(g_audio_tid, NULL);
        g_audio_started = 0;
    }
    g_write_handler = NULL;
    g_reserved      = NULL;
}

#endif /* !WIN32 */
