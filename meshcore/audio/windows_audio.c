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
 * Windows WASAPI loopback audio capture for MeshAgent desktop audio streaming.
 *
 * Captures whatever audio is playing on the default audio output device
 * and streams it as MNG_AUDIO_DATA packets through the KVM write handler.
 *
 * Format negotiated with Windows; converted to PCM S16LE for browser compatibility.
 * MNG_AUDIO_INFO is sent once at stream start so the browser knows the sample rate.
 */

#ifdef WIN32

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>

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

// ---- Thread state ----
static HANDLE            g_audio_thread   = NULL;
static volatile int      g_audio_shutdown = 0;
static AUDIO_WriteHandler g_write_handler = NULL;
static void              *g_reserved      = NULL;

// ---- Packet helpers ----

// Clamp and convert a 32-bit float sample to signed 16-bit
static __inline short float_to_s16(float f)
{
    int v = (int)(f * 32767.0f);
    if (v >  32767) return  32767;
    if (v < -32768) return -32768;
    return (short)v;
}

// Build and send MNG_AUDIO_INFO packet (tells the browser the audio format)
static void send_audio_info(UINT32 sampleRate, WORD channels, WORD bitsPerSample)
{
    char buf[12];
    ((unsigned short*)buf)[0] = htons((unsigned short)MNG_AUDIO_INFO);
    ((unsigned short*)buf)[1] = htons(12);
    ((unsigned int*) buf)[1]  = htonl(sampleRate);
    ((unsigned short*)buf)[4] = htons(channels);
    ((unsigned short*)buf)[5] = htons(bitsPerSample);
    g_write_handler(buf, 12, g_reserved);
}

// Build and send MNG_AUDIO_DATA packet
// data_s16: PCM S16LE samples (interleaved channels), nFrames: sample frames
static void send_audio_data(const short *data_s16, UINT32 nFrames, WORD channels)
{
    if (nFrames == 0) return;
    int pcm_bytes  = (int)(nFrames * channels * 2); // 2 bytes per S16 sample
    int total_size = 8 + pcm_bytes;                  // 4-byte header + 4-byte timestamp + PCM
    char *pkt = (char*)malloc(total_size);
    if (!pkt) return;

    ((unsigned short*)pkt)[0] = htons((unsigned short)MNG_AUDIO_DATA);
    ((unsigned short*)pkt)[1] = htons((unsigned short)total_size);
    ((unsigned int*) pkt)[1]  = htonl((unsigned int)GetTickCount());
    memcpy(pkt + 8, data_s16, pcm_bytes);
    g_write_handler(pkt, total_size, g_reserved);
    free(pkt);
}

// ---- WASAPI capture thread ----
static DWORD WINAPI audio_capture_thread(LPVOID arg)
{
    HRESULT              hr;
    IMMDeviceEnumerator *pEnum   = NULL;
    IMMDevice           *pDevice = NULL;
    IAudioClient        *pClient = NULL;
    IAudioCaptureClient *pCapture= NULL;
    WAVEFORMATEX        *pwfx    = NULL;
    UINT32               bufferFrames = 0;
    DWORD                taskIdx = 0;
    HANDLE               hTask   = NULL;
    short               *convert_buf = NULL;
    UINT32               convert_buf_frames = 0;

    if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) goto cleanup;

    // Get the default audio playback (render) device
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator_Audio, NULL, CLSCTX_ALL,
                          &IID_IMMDeviceEnumerator_Audio, (void**)&pEnum);
    if (FAILED(hr)) goto cleanup_coinit;

    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(pEnum, eRender, eConsole, &pDevice);
    if (FAILED(hr)) goto cleanup_enum;

    hr = IMMDevice_Activate(pDevice, &IID_IAudioClient_Audio, CLSCTX_ALL, NULL, (void**)&pClient);
    if (FAILED(hr)) goto cleanup_device;

    // Get the device's native mix format
    hr = IAudioClient_GetMixFormat(pClient, &pwfx);
    if (FAILED(hr)) goto cleanup_client;

    UINT32 nSampleRate = pwfx->nSamplesPerSec;
    WORD   nChannels   = pwfx->nChannels;
    WORD   wBits       = pwfx->wBitsPerSample;
    int    is_float    = (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                          (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wBits == 32));

    // Initialize in shared-mode loopback
    hr = IAudioClient_Initialize(pClient, AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_LOOPBACK,
                                 200 * 10000, // 200ms buffer (100ns units)
                                 0, pwfx, NULL);
    if (FAILED(hr)) goto cleanup_format;

    hr = IAudioClient_GetService(pClient, &IID_IAudioCaptureClient_Audio, (void**)&pCapture);
    if (FAILED(hr)) goto cleanup_format;

    hr = IAudioClient_GetBufferSize(pClient, &bufferFrames);
    if (FAILED(hr)) goto cleanup_capture;

    // Boost thread priority for audio (optional, non-fatal)
    hTask = AvSetMmThreadCharacteristicsW(L"Audio", &taskIdx);

    hr = IAudioClient_Start(pClient);
    if (FAILED(hr)) goto cleanup_capture;

    // Tell the browser what audio format we're sending
    send_audio_info(nSampleRate, nChannels, 16); // we always output S16

    // Pre-allocate conversion buffer for float→S16
    convert_buf_frames = bufferFrames;
    convert_buf = (short*)malloc(convert_buf_frames * nChannels * sizeof(short));

    while (!g_audio_shutdown)
    {
        UINT32  packet_frames = 0;
        HRESULT hr2 = IAudioCaptureClient_GetNextPacketSize(pCapture, &packet_frames);
        if (FAILED(hr2)) break;

        while (packet_frames > 0 && !g_audio_shutdown)
        {
            BYTE  *pData       = NULL;
            UINT32 num_frames  = 0;
            DWORD  flags       = 0;

            hr2 = IAudioCaptureClient_GetBuffer(pCapture, &pData, &num_frames, &flags, NULL, NULL);
            if (FAILED(hr2)) break;

            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && num_frames > 0)
            {
                // Resize conversion buffer if needed
                if (num_frames > convert_buf_frames)
                {
                    free(convert_buf);
                    convert_buf_frames = num_frames;
                    convert_buf = (short*)malloc(convert_buf_frames * nChannels * sizeof(short));
                }

                if (convert_buf)
                {
                    UINT32 total_samples = num_frames * nChannels;
                    if (is_float)
                    {
                        // IEEE float32 → S16
                        const float *src = (const float*)pData;
                        for (UINT32 i = 0; i < total_samples; i++)
                            convert_buf[i] = float_to_s16(src[i]);
                    }
                    else
                    {
                        // Assume already S16 (PCM)
                        memcpy(convert_buf, pData, total_samples * sizeof(short));
                    }
                    send_audio_data(convert_buf, num_frames, nChannels);
                }
            }
            else if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
            {
                // Send silence so the browser stream doesn't stall
                if (num_frames > 0 && convert_buf)
                {
                    memset(convert_buf, 0, num_frames * nChannels * sizeof(short));
                    send_audio_data(convert_buf, num_frames, nChannels);
                }
            }

            IAudioCaptureClient_ReleaseBuffer(pCapture, num_frames);
            hr2 = IAudioCaptureClient_GetNextPacketSize(pCapture, &packet_frames);
            if (FAILED(hr2)) break;
        }

        Sleep(10); // 10ms polling
    }

    IAudioClient_Stop(pClient);

cleanup_capture:
    if (pCapture) { IAudioCaptureClient_Release(pCapture); }
cleanup_format:
    if (pwfx)     { CoTaskMemFree(pwfx); }
    if (convert_buf) { free(convert_buf); }
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

void audio_relay_setup(AUDIO_WriteHandler writeHandler, void *reserved, void *chain)
{
    if (g_audio_thread != NULL)
    {
        // Check if thread is still alive; it may have exited silently due to a
        // WASAPI init failure (no audio device, driver error, etc.).
        // If dead, clean up and fall through to restart capture.
        DWORD exitCode = STILL_ACTIVE;
        GetExitCodeThread(g_audio_thread, &exitCode);
        if (exitCode == STILL_ACTIVE) return; // genuinely running
        CloseHandle(g_audio_thread);
        g_audio_thread = NULL;
    }

    g_write_handler  = writeHandler;
    g_reserved       = reserved;
    g_audio_shutdown = 0;

    // Send MNG_AUDIO_INFO immediately from the calling thread so the browser
    // can initialize its AudioContext right away without waiting for WASAPI.
    // The capture thread will send another INFO with the actual device format;
    // the browser handles duplicate INFO packets by ignoring identical ones.
    send_audio_info(48000, 2, 16);

    g_audio_thread = CreateThread(NULL, 0, audio_capture_thread, NULL, 0, NULL);
}

void audio_relay_stop(void)
{
    g_audio_shutdown = 1;
    if (g_audio_thread != NULL)
    {
        WaitForSingleObject(g_audio_thread, 3000);
        CloseHandle(g_audio_thread);
        g_audio_thread = NULL;
    }
    g_write_handler = NULL;
    g_reserved      = NULL;
}

#endif /* WIN32 */
