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

#pragma once
#ifndef __AUDIO_RELAY_H__
#define __AUDIO_RELAY_H__

#include "microstack/ILibParsers.h"

// Compatible with ILibKVM_WriteHandler / ILibTransport_DoneState (0=COMPLETE, 1=INCOMPLETE, -1=ERROR)
typedef ILibTransport_DoneState(*AUDIO_WriteHandler)(char *buffer, int bufferLen, void *reserved);

// Audio sources
#define AUDIO_SOURCE_SYSTEM 0   // what the machine is playing (loopback / monitor)
#define AUDIO_SOURCE_MIC    1   // the machine's default microphone / capture input
#define AUDIO_SOURCE_COUNT  2

// Quality levels (browser-controllable, adaptive). Higher = less bandwidth.
//   0 = native rate, native channels (full quality)
//   1 = native rate, mono
//   2 = native/2, mono
//   3 = native/3, mono
//   4 = native/6, mono (voice)
#define AUDIO_QUALITY_MAX_LEVEL 4

//
// audio_relay_setup - Start capture for one source.
//
//   writeHandler : callback used to push MNG_AUDIO_DATA/INFO packets to the KVM channel
//   reserved     : opaque pointer forwarded to writeHandler
//   chain        : microstack chain pointer (reserved for future use)
//   sourceId     : AUDIO_SOURCE_SYSTEM or AUDIO_SOURCE_MIC
//   quality      : initial quality level (0..AUDIO_QUALITY_MAX_LEVEL)
//
void audio_relay_setup(AUDIO_WriteHandler writeHandler, void *reserved, void *chain, int sourceId, int quality);

//
// audio_relay_set_quality - Change the quality level of a running source on the fly.
//
void audio_relay_set_quality(int sourceId, int level);

//
// audio_relay_stop_source - Stop a single capture source.
//
void audio_relay_stop_source(int sourceId);

//
// audio_relay_stop - Stop ALL capture sources and free resources (cleanup).
//
void audio_relay_stop(void);

#endif /* __AUDIO_RELAY_H__ */
