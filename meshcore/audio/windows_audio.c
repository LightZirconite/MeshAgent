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
 * Windows WASAPI audio capture for MeshAgent desktop audio streaming.
 *
 * Two independent sources can run at the same time:
 *   - AUDIO_SOURCE_SYSTEM : loopback of the default render device (what plays on the PC)
 *   - AUDIO_SOURCE_MIC    : the default capture device (microphone)
 *
 * Each source streams PCM S16LE as MNG_AUDIO_DATA packets, tagged with its
 * sourceId, through the KVM write handler. A MNG_AUDIO_INFO packet announces
 * the (possibly downsampled) format whenever it changes.
 *
 * Bandwidth is adaptive: the browser sends a quality "level" (0..4); the agent
 * decimates (integer factor) and/or downmixes to mono to shrink the stream.
 */

#ifdef WIN32

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdint.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmreg.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")

#include "audio_relay.h"
#include "meshcore/meshdefines.h"

// ---- GUIDs required for CoCreateInstance ----
DEFINE_GUID(CLSID_MMDeviceEnumerator_Audio,
    0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
DEFINE_GUID(IID_IMMDeviceEnumerator_Audio,
    0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
DEFINE_GUID(IID_IAudioClient_Audio,
    0x1CB9AD4C, 0xDBFA, 0x4C32, 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2);
DEFINE_GUID(IID_IAudioCaptureClient_Audio,
    0xC8ADBD64, 0xE71E, 0x48A0, 0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17);

static const GUID AUDIO_SUBTYPE_PCM =
    { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
static const GUID AUDIO_SUBTYPE_IEEE_FLOAT =
    { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };

// ---- Per-source state ----
typedef struct AudioSource
{
    int           sourceId;       // AUDIO_SOURCE_SYSTEM / AUDIO_SOURCE_MIC
    HANDLE        thread;
    volatile LONG shutdown;
    volatile LONG level;          // current quality level (Interlocked)
} AudioSource;

static AudioSource        g_src[AUDIO_SOURCE_COUNT];
static AUDIO_WriteHandler g_write_handler = NULL;
static void              *g_reserved      = NULL;

// ---- Helpers ----

static __inline short clamp_s16(long long v)
{
    if (v >  32767) return  32767;
    if (v < -32768) return -32768;
    return (short)v;
}

static int wave_format_kind(const WAVEFORMATEX *format, WORD *validBits)
{
    const WAVEFORMATEXTENSIBLE *ext;

    *validBits = format->wBitsPerSample;
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return 2;
    if (format->wFormatTag == WAVE_FORMAT_PCM) return 1;
    if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
        format->cbSize < (sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)))
    {
        return 0;
    }

    ext = (const WAVEFORMATEXTENSIBLE*)format;
    if (ext->Samples.wValidBitsPerSample > 0 &&
        ext->Samples.wValidBitsPerSample <= format->wBitsPerSample)
    {
        *validBits = ext->Samples.wValidBitsPerSample;
    }
    if (IsEqualGUID(&ext->SubFormat, &AUDIO_SUBTYPE_IEEE_FLOAT)) return 2;
    if (IsEqualGUID(&ext->SubFormat, &AUDIO_SUBTYPE_PCM)) return 1;
    return 0;
}

static short read_sample_s16(const BYTE *data, UINT32 frame, WORD inputChannels,
                             WORD channel, WORD containerBits, WORD validBits, int formatKind)
{
    const BYTE *sample;
    UINT32 bytesPerSample = containerBits / 8;
    long value;

    sample = data + (((size_t)frame * inputChannels + channel) * bytesPerSample);
    if (formatKind == 2)
    {
        double valueFloat;
        if (containerBits == 32)
        {
            float f;
            memcpy(&f, sample, sizeof(f));
            valueFloat = f;
        }
        else if (containerBits == 64)
        {
            double d;
            memcpy(&d, sample, sizeof(d));
            valueFloat = d;
        }
        else
        {
            return 0;
        }
        if (valueFloat > 1.0) valueFloat = 1.0;
        if (valueFloat < -1.0) valueFloat = -1.0;
        return clamp_s16((long long)(valueFloat * 32767.0));
    }

    switch (containerBits)
    {
    case 8:
        return (short)(((int)sample[0] - 128) << 8);
    case 16:
    {
        short s;
        memcpy(&s, sample, sizeof(s));
        return s;
    }
    case 24:
        value = (long)(sample[0] | (sample[1] << 8) | (sample[2] << 16));
        if (value & 0x00800000L) value |= ~0x00FFFFFFL;
        return (short)(value >> 8);
    case 32:
    {
        int32_t s;
        memcpy(&s, sample, sizeof(s));
        (void)validBits;
        return clamp_s16((long long)(s >> 16));
    }
    default:
        return 0;
    }
}

// Map a quality level to a decimation factor and a mono flag.
static void level_to_params(int level, int *factor, int *mono)
{
    switch (level)
    {
    case 0:  *factor = 1; *mono = 0; break; // native rate, native channels
    case 1:  *factor = 1; *mono = 1; break; // native rate, mono
    case 2:  *factor = 2; *mono = 1; break; // half rate, mono
    case 3:  *factor = 3; *mono = 1; break; // third rate, mono
    default: *factor = 6; *mono = 1; break; // sixth rate, mono (voice)
    }
}

// Build and send MNG_AUDIO_INFO (13 bytes): rate(4) channels(2) bits(2) sourceId(1)
static void send_audio_info(int sourceId, UINT32 sampleRate, WORD channels, WORD bitsPerSample)
{
    char buf[13];
    if (!g_write_handler) return;
    ((unsigned short*)buf)[0] = htons((unsigned short)MNG_AUDIO_INFO);
    ((unsigned short*)buf)[1] = htons(13);
    ((unsigned int*) buf)[1]  = htonl(sampleRate);
    ((unsigned short*)buf)[4] = htons(channels);
    ((unsigned short*)buf)[5] = htons(bitsPerSample);
    buf[12] = (char)(unsigned char)sourceId;
    g_write_handler(buf, 13, g_reserved);
}

// Build and send MNG_AUDIO_DATA. Header (8 bytes): type(2) size(2) sourceId(1) flags(1) seq(2),
// followed by interleaved PCM S16LE. data_s16 holds nFrames * channels samples.
static void send_audio_data(int sourceId, const short *data_s16, UINT32 nFrames, WORD channels, unsigned short seq)
{
    int pcm_bytes, total_size;
    char *pkt;
    if (nFrames == 0 || !g_write_handler) return;
    pcm_bytes  = (int)(nFrames * channels * 2);
    total_size = 8 + pcm_bytes;
    if (total_size > 65535) return; // size field is 16-bit; browser requests lower quality if needed
    pkt = (char*)malloc(total_size);
    if (!pkt) return;

    ((unsigned short*)pkt)[0] = htons((unsigned short)MNG_AUDIO_DATA);
    ((unsigned short*)pkt)[1] = htons((unsigned short)total_size);
    pkt[4] = (char)(unsigned char)sourceId;
    pkt[5] = 0; // flags (reserved)
    ((unsigned short*)pkt)[3] = htons(seq);
    memcpy(pkt + 8, data_s16, pcm_bytes);
    g_write_handler(pkt, total_size, g_reserved);
    free(pkt);
}

// ---- WASAPI capture thread (shared by both sources) ----
static DWORD WINAPI audio_capture_thread(LPVOID arg)
{
    AudioSource         *src = (AudioSource*)arg;
    HRESULT              hr;
    IMMDeviceEnumerator *pEnum    = NULL;
    IMMDevice           *pDevice  = NULL;
    IAudioClient        *pClient  = NULL;
    IAudioCaptureClient *pCapture = NULL;
    WAVEFORMATEX        *pwfx     = NULL;
    UINT32               bufferFrames = 0;
    DWORD                taskIdx  = 0;
    HANDLE               hTask    = NULL;
    HANDLE               hAudioEvent = NULL;
    short               *outBuf   = NULL;
    UINT32               outCap   = 0;       // capacity of outBuf in frames
    EDataFlow            flow     = (src->sourceId == AUDIO_SOURCE_MIC) ? eCapture : eRender;
    ERole                role     = (src->sourceId == AUDIO_SOURCE_MIC) ? eCommunications : eConsole;
    DWORD                streamFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                       ((src->sourceId == AUDIO_SOURCE_MIC) ? 0 : AUDCLNT_STREAMFLAGS_LOOPBACK);

    // Decimation accumulator state (carried across WASAPI buffers)
    long long acc[8];
    int       accN = 0;
    int       curLevel = -1, factor = 1, mono = 0;
    UINT32    nSampleRate = 48000;
    WORD      inputChannels = 2;
    WORD      processChannels = 2;
    WORD      containerBits = 16;
    WORD      validBits = 16;
    int       formatKind = 0;
    unsigned short seq = 0;
    int       i;

    for (i = 0; i < 8; i++) acc[i] = 0;

    if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) goto cleanup;

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator_Audio, NULL, CLSCTX_ALL,
                          &IID_IMMDeviceEnumerator_Audio, (void**)&pEnum);
    if (FAILED(hr)) goto cleanup_coinit;

    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(pEnum, flow, role, &pDevice);
    if (FAILED(hr) && role != eConsole)
    {
        hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(pEnum, flow, eConsole, &pDevice);
    }
    if (FAILED(hr)) goto cleanup_enum;

    hr = IMMDevice_Activate(pDevice, &IID_IAudioClient_Audio, CLSCTX_ALL, NULL, (void**)&pClient);
    if (FAILED(hr)) goto cleanup_device;

    hr = IAudioClient_GetMixFormat(pClient, &pwfx);
    if (FAILED(hr)) goto cleanup_client;

    nSampleRate = pwfx->nSamplesPerSec;
    inputChannels = pwfx->nChannels;
    if (inputChannels < 1) goto cleanup_format;
    processChannels = inputChannels > 8 ? 8 : inputChannels;
    containerBits = pwfx->wBitsPerSample;
    formatKind = wave_format_kind(pwfx, &validBits);
    if (formatKind == 0 || containerBits == 0 || (containerBits % 8) != 0) goto cleanup_format;

    hr = IAudioClient_Initialize(pClient, AUDCLNT_SHAREMODE_SHARED, streamFlags,
                                 100 * 10000, 0, pwfx, NULL);
    if (FAILED(hr)) goto cleanup_format;

    hAudioEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (hAudioEvent == NULL) goto cleanup_format;
    hr = IAudioClient_SetEventHandle(pClient, hAudioEvent);
    if (FAILED(hr)) goto cleanup_event;

    hr = IAudioClient_GetService(pClient, &IID_IAudioCaptureClient_Audio, (void**)&pCapture);
    if (FAILED(hr)) goto cleanup_event;

    hr = IAudioClient_GetBufferSize(pClient, &bufferFrames);
    if (FAILED(hr)) goto cleanup_capture;

    hTask = AvSetMmThreadCharacteristicsW(L"Audio", &taskIdx);

    hr = IAudioClient_Start(pClient);
    if (FAILED(hr)) goto cleanup_capture;

    while (!src->shutdown)
    {
        UINT32  packet_frames = 0;
        HRESULT hr2 = IAudioCaptureClient_GetNextPacketSize(pCapture, &packet_frames);
        if (FAILED(hr2)) break;

        // Apply a pending quality change (also done before first packet)
        {
            int lvl = (int)InterlockedCompareExchange(&src->level, 0, 0);
            if (lvl != curLevel)
            {
                curLevel = lvl;
                level_to_params(curLevel, &factor, &mono);
                acc[0] = acc[1] = acc[2] = acc[3] = acc[4] = acc[5] = acc[6] = acc[7] = 0;
                accN = 0;
                send_audio_info(src->sourceId, nSampleRate / (UINT32)factor,
                                (WORD)(mono ? 1 : processChannels), 16);
            }
        }

        while (packet_frames > 0 && !src->shutdown)
        {
            BYTE  *pData      = NULL;
            UINT32 num_frames = 0;
            DWORD  flags      = 0;
            UINT32 outFrames  = 0;
            WORD   effCh      = (WORD)(mono ? 1 : processChannels);
            UINT32 needFrames;

            hr2 = IAudioCaptureClient_GetBuffer(pCapture, &pData, &num_frames, &flags, NULL, NULL);
            if (FAILED(hr2)) break;

            // Ensure output buffer can hold the worst case for this packet
            needFrames = (num_frames / (UINT32)factor) + 2;
            if (needFrames > outCap)
            {
                short *nb = (short*)realloc(outBuf, needFrames * 8 * sizeof(short));
                if (nb) { outBuf = nb; outCap = needFrames; }
            }

            if (num_frames > 0 && outBuf)
            {
                int silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) ? 1 : 0;
                UINT32 f;
                for (f = 0; f < num_frames; f++)
                {
                    int ch;
                    for (ch = 0; ch < processChannels; ch++)
                    {
                        int s = 0;
                        if (!silent)
                        {
                            s = read_sample_s16(pData, f, inputChannels, (WORD)ch,
                                                containerBits, validBits, formatKind);
                        }
                        acc[ch] += s;
                    }
                    if (++accN >= factor)
                    {
                        if (mono)
                        {
                            long long sum = 0;
                            for (ch = 0; ch < processChannels; ch++) { sum += acc[ch]; acc[ch] = 0; }
                            outBuf[outFrames] = clamp_s16(sum / ((long long)factor * processChannels));
                            outFrames++;
                        }
                        else
                        {
                            for (ch = 0; ch < processChannels; ch++)
                            {
                                outBuf[outFrames * processChannels + ch] = clamp_s16(acc[ch] / factor);
                                acc[ch] = 0;
                            }
                            outFrames++;
                        }
                        accN = 0;
                    }
                }
                if (outFrames > 0)
                    send_audio_data(src->sourceId, outBuf, outFrames, effCh, seq++);
            }

            IAudioCaptureClient_ReleaseBuffer(pCapture, num_frames);
            hr2 = IAudioCaptureClient_GetNextPacketSize(pCapture, &packet_frames);
            if (FAILED(hr2)) break;
        }

        if (!src->shutdown) WaitForSingleObject(hAudioEvent, 100);
    }

    IAudioClient_Stop(pClient);

cleanup_capture:
    if (pCapture) { IAudioCaptureClient_Release(pCapture); }
cleanup_event:
    if (hAudioEvent) { CloseHandle(hAudioEvent); }
cleanup_format:
    if (pwfx)     { CoTaskMemFree(pwfx); }
    if (outBuf)   { free(outBuf); }
cleanup_client:
    if (pClient)  { IAudioClient_Release(pClient); }
cleanup_device:
    if (pDevice)  { IMMDevice_Release(pDevice); }
cleanup_enum:
    if (pEnum)    { IMMDeviceEnumerator_Release(pEnum); }
    if (hTask)    { AvRevertMmThreadCharacteristics(hTask); }
cleanup_coinit:
    CoUninitialize();
cleanup:
    return 0;
}

// ---- Public API ----

static int source_valid(int sourceId)
{
    return (sourceId >= 0 && sourceId < AUDIO_SOURCE_COUNT);
}

void audio_relay_setup(AUDIO_WriteHandler writeHandler, void *reserved, void *chain, int sourceId, int quality)
{
    AudioSource *src;
    (void)chain;
    if (!source_valid(sourceId)) return;
    if (quality < 0) quality = 0;
    if (quality > AUDIO_QUALITY_MAX_LEVEL) quality = AUDIO_QUALITY_MAX_LEVEL;

    src = &g_src[sourceId];
    src->sourceId = sourceId;

    // The write handler is shared by all sources (single KVM channel back to the browser).
    g_write_handler = writeHandler;
    g_reserved      = reserved;

    if (src->thread != NULL)
    {
        DWORD exitCode = STILL_ACTIVE;
        GetExitCodeThread(src->thread, &exitCode);
        if (exitCode == STILL_ACTIVE)
        {
            // Already running: treat a fresh START as a quality update.
            InterlockedExchange(&src->level, quality);
            return;
        }
        CloseHandle(src->thread);
        src->thread = NULL;
    }

    InterlockedExchange(&src->level, quality);
    src->shutdown = 0;

    // Send an immediate INFO so the browser can prepare its AudioContext without
    // waiting for WASAPI. The thread re-sends INFO with the real device rate.
    send_audio_info(sourceId, 48000, 2, 16);

    src->thread = CreateThread(NULL, 0, audio_capture_thread, src, 0, NULL);
}

void audio_relay_set_quality(int sourceId, int level)
{
    if (!source_valid(sourceId)) return;
    if (level < 0) level = 0;
    if (level > AUDIO_QUALITY_MAX_LEVEL) level = AUDIO_QUALITY_MAX_LEVEL;
    InterlockedExchange(&g_src[sourceId].level, level);
}

void audio_relay_stop_source(int sourceId)
{
    int i, anyActive;
    AudioSource *src;
    if (!source_valid(sourceId)) return;
    src = &g_src[sourceId];

    src->shutdown = 1;
    if (src->thread != NULL)
    {
        WaitForSingleObject(src->thread, 3000);
        CloseHandle(src->thread);
        src->thread = NULL;
    }

    // Drop the shared handler only when no source remains active.
    anyActive = 0;
    for (i = 0; i < AUDIO_SOURCE_COUNT; i++)
        if (g_src[i].thread != NULL) anyActive = 1;
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
        if (g_src[i].thread != NULL)
        {
            WaitForSingleObject(g_src[i].thread, 3000);
            CloseHandle(g_src[i].thread);
            g_src[i].thread = NULL;
        }
    }
    g_write_handler = NULL;
    g_reserved      = NULL;
}

#endif /* WIN32 */
