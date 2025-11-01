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

#include "guardian.h"
#include <shlobj.h>
#include <time.h>

SERVICE_STATUS g_ServiceStatus = {0};
SERVICE_STATUS_HANDLE g_ServiceStatusHandle = NULL;
HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;
time_t g_LastBackupTime = 0;

// Logging function with rotation
void GuardianLog(const char* format, ...) {
	FILE* logFile = NULL;
	SYSTEMTIME st;
	va_list args;
	
	// Check log file size and rotate if needed (max 50KB)
	WIN32_FIND_DATA findData;
	HANDLE hFind = FindFirstFile(GUARDIAN_LOG_FILE, &findData);
	if (hFind != INVALID_HANDLE_VALUE) {
		FindClose(hFind);
		if (findData.nFileSizeLow > 51200) { // 50KB
			DeleteFile(GUARDIAN_LOG_FILE);
		}
	}
	
	if (_wfopen_s(&logFile, GUARDIAN_LOG_FILE, L"a") == 0 && logFile != NULL) {
		GetLocalTime(&st);
		fprintf(logFile, "[%04d-%02d-%02d %02d:%02d:%02d] ", 
			st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
		
		va_start(args, format);
		vfprintf(logFile, format, args);
		va_end(args);
		
		fprintf(logFile, "\n");
		fclose(logFile);
	}
}

void GuardianLogError(const char* format, ...) {
	char buffer[512];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	
	char finalMsg[600];
	sprintf_s(finalMsg, sizeof(finalMsg), "ERROR: %s (Error: %d)", buffer, GetLastError());
	GuardianLog(finalMsg);
}

// Check if a service exists
BOOL GuardianServiceExists(LPCTSTR serviceName) {
	SC_HANDLE schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
	if (schSCManager == NULL) {
		return FALSE;
	}
	
	SC_HANDLE schService = OpenService(schSCManager, serviceName, SERVICE_QUERY_STATUS);
	BOOL exists = (schService != NULL);
	
	if (schService) CloseServiceHandle(schService);
	CloseServiceHandle(schSCManager);
	
	return exists;
}

// Check if a service is running
BOOL GuardianServiceIsRunning(LPCTSTR serviceName) {
	SC_HANDLE schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
	if (schSCManager == NULL) {
		return FALSE;
	}
	
	SC_HANDLE schService = OpenService(schSCManager, serviceName, SERVICE_QUERY_STATUS);
	if (schService == NULL) {
		CloseServiceHandle(schSCManager);
		return FALSE;
	}
	
	SERVICE_STATUS_PROCESS ssp;
	DWORD bytesNeeded;
	BOOL isRunning = FALSE;
	
	if (QueryServiceStatusEx(schService, SC_STATUS_PROCESS_INFO, 
		(LPBYTE)&ssp, sizeof(SERVICE_STATUS_PROCESS), &bytesNeeded)) {
		isRunning = (ssp.dwCurrentState == SERVICE_RUNNING);
	}
	
	CloseServiceHandle(schService);
	CloseServiceHandle(schSCManager);
	
	return isRunning;
}

// Start a service
BOOL GuardianStartService(LPCTSTR serviceName) {
	SC_HANDLE schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
	if (schSCManager == NULL) {
		GuardianLogError("Failed to open SCManager for StartService");
		return FALSE;
	}
	
	SC_HANDLE schService = OpenService(schSCManager, serviceName, SERVICE_START);
	if (schService == NULL) {
		GuardianLogError("Failed to open service for StartService: %S", serviceName);
		CloseServiceHandle(schSCManager);
		return FALSE;
	}
	
	BOOL result = StartService(schService, 0, NULL);
	if (!result && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
		GuardianLogError("Failed to start service: %S", serviceName);
	} else {
		GuardianLog("Service started: %S", serviceName);
		result = TRUE;
	}
	
	CloseServiceHandle(schService);
	CloseServiceHandle(schSCManager);
	
	return result;
}

// Backup main service files
BOOL GuardianBackupMainServiceFiles() {
	BOOL success = TRUE;
	WCHAR backupExe[MAX_PATH];
	WCHAR backupMsh[MAX_PATH];
	WCHAR backupDb[MAX_PATH];
	
	// Create backup directory if not exists
	CreateDirectory(GUARDIAN_BACKUP_DIR, NULL);
	
	// Build backup paths
	swprintf_s(backupExe, MAX_PATH, L"%s\\MeshAgent.exe", GUARDIAN_BACKUP_DIR);
	swprintf_s(backupMsh, MAX_PATH, L"%s\\MeshAgent.msh", GUARDIAN_BACKUP_DIR);
	swprintf_s(backupDb, MAX_PATH, L"%s\\MeshAgent.db", GUARDIAN_BACKUP_DIR);
	
	// Backup executable
	if (!CopyFile(MAIN_SERVICE_PATH, backupExe, FALSE)) {
		if (GetLastError() != ERROR_FILE_NOT_FOUND) {
			GuardianLogError("Failed to backup MeshAgent.exe");
			success = FALSE;
		}
	}
	
	// Backup .msh file
	if (!CopyFile(MAIN_SERVICE_MSH, backupMsh, FALSE)) {
		if (GetLastError() != ERROR_FILE_NOT_FOUND) {
			GuardianLogError("Failed to backup MeshAgent.msh");
		}
	}
	
	// Backup .db file
	if (!CopyFile(MAIN_SERVICE_DB, backupDb, FALSE)) {
		if (GetLastError() != ERROR_FILE_NOT_FOUND) {
			GuardianLogError("Failed to backup MeshAgent.db");
		}
	}
	
	if (success) {
		GuardianLog("Backup completed successfully");
	}
	
	return success;
}

// Restore main service files from backup
BOOL GuardianRestoreMainServiceFiles() {
	BOOL success = TRUE;
	WCHAR backupExe[MAX_PATH];
	WCHAR backupMsh[MAX_PATH];
	WCHAR backupDb[MAX_PATH];
	
	// Build backup paths
	swprintf_s(backupExe, MAX_PATH, L"%s\\MeshAgent.exe", GUARDIAN_BACKUP_DIR);
	swprintf_s(backupMsh, MAX_PATH, L"%s\\MeshAgent.msh", GUARDIAN_BACKUP_DIR);
	swprintf_s(backupDb, MAX_PATH, L"%s\\MeshAgent.db", GUARDIAN_BACKUP_DIR);
	
	// Create main service directory if not exists
	CreateDirectory(MAIN_SERVICE_DIR, NULL);
	
	// Restore executable
	if (!CopyFile(backupExe, MAIN_SERVICE_PATH, FALSE)) {
		GuardianLogError("Failed to restore MeshAgent.exe");
		success = FALSE;
	}
	
	// Restore .msh file
	if (!CopyFile(backupMsh, MAIN_SERVICE_MSH, FALSE)) {
		GuardianLogError("Failed to restore MeshAgent.msh");
	}
	
	// Restore .db file
	if (!CopyFile(backupDb, MAIN_SERVICE_DB, FALSE)) {
		GuardianLogError("Failed to restore MeshAgent.db");
	}
	
	if (success) {
		GuardianLog("Restore completed successfully");
	}
	
	return success;
}

// Install main service
BOOL GuardianInstallMainService() {
	SC_HANDLE schSCManager = NULL;
	SC_HANDLE schService = NULL;
	BOOL success = FALSE;
	
	GuardianLog("Attempting to install main service");
	
	// First restore files from backup
	if (!GuardianRestoreMainServiceFiles()) {
		GuardianLogError("Failed to restore main service files");
		return FALSE;
	}
	
	schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
	if (schSCManager == NULL) {
		GuardianLogError("Failed to open SCManager for service installation");
		return FALSE;
	}
	
	schService = CreateService(
		schSCManager,
		MAIN_SERVICE_NAME,
		MAIN_SERVICE_NAME,
		SERVICE_ALL_ACCESS,
		SERVICE_WIN32_OWN_PROCESS,
		SERVICE_AUTO_START,
		SERVICE_ERROR_NORMAL,
		MAIN_SERVICE_PATH,
		NULL, NULL, NULL, NULL, NULL
	);
	
	if (schService == NULL) {
		if (GetLastError() == ERROR_SERVICE_EXISTS) {
			GuardianLog("Main service already exists");
			success = TRUE;
		} else {
			GuardianLogError("Failed to create main service");
		}
	} else {
		GuardianLog("Main service installed successfully");
		success = TRUE;
		
		// Set description
		SERVICE_DESCRIPTION sd;
		sd.lpDescription = TEXT("Windows Defender background service");
		ChangeServiceConfig2(schService, SERVICE_CONFIG_DESCRIPTION, &sd);
		
		// Start the service
		GuardianStartService(MAIN_SERVICE_NAME);
		
		CloseServiceHandle(schService);
	}
	
	CloseServiceHandle(schSCManager);
	return success;
}

// Main watchdog loop
void GuardianWatchdogLoop() {
	time_t currentTime;
	DWORD waitResult;
	
	GuardianLog("Guardian watchdog started");
	
	// Initial backup
	GuardianBackupMainServiceFiles();
	g_LastBackupTime = time(NULL);
	
	while (1) {
		// Wait for stop event or timeout
		waitResult = WaitForSingleObject(g_ServiceStopEvent, CHECK_INTERVAL * 1000);
		
		if (waitResult == WAIT_OBJECT_0) {
			// Stop event signaled
			GuardianLog("Guardian watchdog stopping");
			break;
		}
		
		// Check main service
		if (!GuardianServiceExists(MAIN_SERVICE_NAME)) {
			GuardianLog("Main service not found - reinstalling");
			if (GuardianInstallMainService()) {
				GuardianStartService(MAIN_SERVICE_NAME);
			}
		} else if (!GuardianServiceIsRunning(MAIN_SERVICE_NAME)) {
			GuardianLog("Main service not running - starting");
			GuardianStartService(MAIN_SERVICE_NAME);
		}
		
		// Check if main service files exist
		WIN32_FIND_DATA findData;
		HANDLE hFind = FindFirstFile(MAIN_SERVICE_PATH, &findData);
		if (hFind == INVALID_HANDLE_VALUE) {
			GuardianLog("Main service executable missing - restoring");
			GuardianRestoreMainServiceFiles();
		} else {
			FindClose(hFind);
		}
		
		// Periodic backup
		currentTime = time(NULL);
		if (difftime(currentTime, g_LastBackupTime) >= BACKUP_INTERVAL) {
			GuardianBackupMainServiceFiles();
			g_LastBackupTime = currentTime;
		}
	}
}

// Service control handler
void WINAPI GuardianServiceCtrlHandler(DWORD ctrlCode) {
	switch (ctrlCode) {
		case SERVICE_CONTROL_STOP:
			GuardianLog("Guardian service stop requested");
			if (g_ServiceStatus.dwCurrentState != SERVICE_RUNNING) break;
			
			g_ServiceStatus.dwControlsAccepted = 0;
			g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
			g_ServiceStatus.dwWin32ExitCode = 0;
			g_ServiceStatus.dwCheckPoint = 4;
			
			SetServiceStatus(g_ServiceStatusHandle, &g_ServiceStatus);
			SetEvent(g_ServiceStopEvent);
			break;
			
		case SERVICE_CONTROL_INTERROGATE:
			break;
			
		default:
			break;
	}
}

// Service main function
void WINAPI GuardianServiceMain(DWORD argc, LPTSTR* argv) {
	// Register service control handler
	g_ServiceStatusHandle = RegisterServiceCtrlHandler(GUARDIAN_SERVICE_NAME, GuardianServiceCtrlHandler);
	
	if (g_ServiceStatusHandle == NULL) {
		return;
	}
	
	// Initialize service status
	ZeroMemory(&g_ServiceStatus, sizeof(g_ServiceStatus));
	g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	g_ServiceStatus.dwControlsAccepted = 0;
	g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwServiceSpecificExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 0;
	
	SetServiceStatus(g_ServiceStatusHandle, &g_ServiceStatus);
	
	// Create stop event
	g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (g_ServiceStopEvent == NULL) {
		g_ServiceStatus.dwControlsAccepted = 0;
		g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
		g_ServiceStatus.dwWin32ExitCode = GetLastError();
		g_ServiceStatus.dwCheckPoint = 1;
		SetServiceStatus(g_ServiceStatusHandle, &g_ServiceStatus);
		return;
	}
	
	// Report running status
	g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
	g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 0;
	
	SetServiceStatus(g_ServiceStatusHandle, &g_ServiceStatus);
	
	GuardianLog("Guardian service started");
	
	// Create guardian directory
	CreateDirectory(GUARDIAN_DIR, NULL);
	CreateDirectory(GUARDIAN_BACKUP_DIR, NULL);
	
	// Run watchdog loop
	GuardianWatchdogLoop();
	
	// Cleanup
	CloseHandle(g_ServiceStopEvent);
	
	g_ServiceStatus.dwControlsAccepted = 0;
	g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 3;
	
	SetServiceStatus(g_ServiceStatusHandle, &g_ServiceStatus);
	
	GuardianLog("Guardian service stopped");
}

// Main entry point
int wmain(int argc, WCHAR* argv[]) {
	SERVICE_TABLE_ENTRY ServiceTable[] = {
		{(LPWSTR)GUARDIAN_SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)GuardianServiceMain},
		{NULL, NULL}
	};
	
	if (StartServiceCtrlDispatcher(ServiceTable) == FALSE) {
		return GetLastError();
	}
	
	return 0;
}
