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

//
// audio_relay_setup - Start desktop audio capture.
//
//   writeHandler : callback used to push MNG_AUDIO_DATA packets to the KVM channel
//   reserved     : opaque pointer forwarded to writeHandler (typically RemoteDesktop_Ptrs*)
//   chain        : microstack chain pointer (unused on Linux, used for future marshalling)
//
void audio_relay_setup(AUDIO_WriteHandler writeHandler, void *reserved, void *chain);

//
// audio_relay_stop - Stop desktop audio capture and free resources.
//
void audio_relay_stop(void);

#endif /* __AUDIO_RELAY_H__ */
