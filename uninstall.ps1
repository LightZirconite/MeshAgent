# Windows Defender Service - Complete Uninstaller
# This script removes both the main service and the guardian service
# Automatically requests administrator privileges if needed

# Check for administrator privileges and request elevation if needed
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    # Relaunch as administrator with UAC prompt
    Write-Host "Requesting administrator privileges..." -ForegroundColor Yellow
    try {
        Start-Process powershell.exe -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"" -Verb RunAs
        exit
    }
    catch {
        Write-Host "ERROR: Failed to elevate privileges!" -ForegroundColor Red
        Write-Host "Please right-click the script and select 'Run as Administrator'" -ForegroundColor Yellow
        pause
        exit 1
    }
}

Write-Host "====================================================" -ForegroundColor Cyan
Write-Host " Windows Defender Service - Complete Uninstaller" -ForegroundColor Cyan
Write-Host "====================================================" -ForegroundColor Cyan
Write-Host ""

Write-Host "[1/6] Stopping all running processes..." -ForegroundColor Yellow

# Stop all possible processes first
$processes = @("MeshAgent", "SystemMonitor", "WinSecHealthSvc")
foreach ($proc in $processes) {
    $running = Get-Process -Name $proc -ErrorAction SilentlyContinue
    if ($running) {
        Write-Host "  - Stopping $proc..." -ForegroundColor Gray
        Stop-Process -Name $proc -Force -ErrorAction SilentlyContinue
    }
}

Start-Sleep -Seconds 2

Write-Host "[2/6] Stopping and uninstalling services..." -ForegroundColor Yellow

# List of all possible service names (old and new)
$serviceNames = @(
    "Mesh Agent",
    "Windows Defender Service", 
    "Windows Security Health Service",
    "WinSecHealthSvc"
)

$uninstalledCount = 0
foreach ($svcName in $serviceNames) {
    $service = Get-Service -Name $svcName -ErrorAction SilentlyContinue
    if ($service) {
        Write-Host "  - Found service: $svcName" -ForegroundColor Cyan
        
        # Stop service
        try {
            Stop-Service $svcName -Force -ErrorAction Stop
            Start-Sleep -Milliseconds 500
        } catch {
            Write-Host "    (Unable to stop, will force delete)" -ForegroundColor Gray
        }
        
        # Delete service
        $result = sc.exe delete $svcName 2>&1
        if ($LASTEXITCODE -eq 0 -or $result -like "*marked for deletion*") {
            Write-Host "    ✅ Service uninstalled" -ForegroundColor Green
            $uninstalledCount++
        } else {
            Write-Host "    ⚠️ $result" -ForegroundColor Yellow
        }
    }
}

if ($uninstalledCount -eq 0) {
    Write-Host "  - No services found to uninstall" -ForegroundColor Gray
} else {
    Write-Host "  - Uninstalled $uninstalledCount service(s)" -ForegroundColor Green
}

Start-Sleep -Seconds 1

Write-Host "[3/6] Removing service files..." -ForegroundColor Yellow

# List of all possible installation directories
$installDirs = @(
    "C:\Program Files\Mesh Agent",
    "C:\Program Files\Windows Defender",
    "C:\Program Files (x86)\Mesh Agent",
    "C:\Program Files (x86)\Windows Defender",
    "C:\Program Files\SystemMonitor",
    "C:\Program Files (x86)\SystemMonitor"
)

$removedCount = 0
foreach ($dir in $installDirs) {
    if (Test-Path $dir) {
        Write-Host "  - Removing $dir..." -ForegroundColor Gray
        try {
            Remove-Item $dir -Recurse -Force -ErrorAction Stop
            Write-Host "    ✅ Removed" -ForegroundColor Green
            $removedCount++
        } catch {
            Write-Host "    ⚠️ $($_.Exception.Message)" -ForegroundColor Yellow
        }
    }
}

if ($removedCount -eq 0) {
    Write-Host "  - No installation directories found" -ForegroundColor Gray
} else {
    Write-Host "  - Removed $removedCount directory(ies)" -ForegroundColor Green
}

Write-Host "[4/6] Removing guardian files..." -ForegroundColor Yellow

# Guardian file locations
$guardianPaths = @(
    "C:\Windows\System32\WinSecHealth",
    "C:\Windows\System32\WinSecHealthSvc.exe",
    "C:\Windows\SysWOW64\WinSecHealth",
    "C:\Windows\SysWOW64\WinSecHealthSvc.exe"
)

$removedGuardianCount = 0
foreach ($path in $guardianPaths) {
    if (Test-Path $path) {
        Write-Host "  - Removing $path..." -ForegroundColor Gray
        try {
            Remove-Item $path -Recurse -Force -ErrorAction Stop
            Write-Host "    ✅ Removed" -ForegroundColor Green
            $removedGuardianCount++
        } catch {
            Write-Host "    ⚠️ $($_.Exception.Message)" -ForegroundColor Yellow
        }
    }
}

if ($removedGuardianCount -eq 0) {
    Write-Host "  - No guardian files found" -ForegroundColor Gray
} else {
    Write-Host "  - Removed $removedGuardianCount guardian file(s)" -ForegroundColor Green
}

Write-Host "[5/6] Cleaning registry..." -ForegroundColor Yellow

# All possible registry paths (old and new installations)
$regPaths = @(
    "HKLM:\SYSTEM\CurrentControlSet\Services\Mesh Agent",
    "HKLM:\SYSTEM\CurrentControlSet\Services\Windows Defender Service",
    "HKLM:\SYSTEM\CurrentControlSet\Services\Windows Security Health Service",
    "HKLM:\SYSTEM\CurrentControlSet\Services\WinSecHealthSvc",
    "HKLM:\Software\Open Source\MeshAgent",
    "HKLM:\Software\Open Source\MeshAgent2"
)

$removedRegCount = 0
foreach ($path in $regPaths) {
    if (Test-Path $path) {
        try {
            Remove-Item $path -Recurse -Force -ErrorAction Stop
            Write-Host "  - Removed: $(Split-Path $path -Leaf)" -ForegroundColor Green
            $removedRegCount++
        } catch {
            Write-Host "  - Unable to remove: $(Split-Path $path -Leaf)" -ForegroundColor Yellow
        }
    }
}

# Remove WOW64 registry keys (32-bit on 64-bit)
try {
    if (Test-Path "HKLM:\Software\WOW6432Node\Open Source\MeshAgent") {
        Remove-Item "HKLM:\Software\WOW6432Node\Open Source\MeshAgent" -Recurse -Force -ErrorAction Stop
        $removedRegCount++
    }
    if (Test-Path "HKLM:\Software\WOW6432Node\Open Source\MeshAgent2") {
        Remove-Item "HKLM:\Software\WOW6432Node\Open Source\MeshAgent2" -Recurse -Force -ErrorAction Stop
        $removedRegCount++
    }
} catch {
    # Silent fail
}

if ($removedRegCount -eq 0) {
    Write-Host "  - No registry keys found" -ForegroundColor Gray
} else {
    Write-Host "  - Removed $removedRegCount registry key(s)" -ForegroundColor Green
}

Write-Host "[6/6] Final cleanup..." -ForegroundColor Yellow

# Kill any remaining processes forcefully
$processesToKill = @("MeshAgent", "SystemMonitor", "WinSecHealthSvc")
$killedCount = 0
foreach ($proc in $processesToKill) {
    $running = Get-Process -Name $proc -ErrorAction SilentlyContinue
    if ($running) {
        Stop-Process -Name $proc -Force -ErrorAction SilentlyContinue
        Write-Host "  - Terminated $proc" -ForegroundColor Green
        $killedCount++
    }
}

if ($killedCount -eq 0) {
    Write-Host "  - No processes to terminate" -ForegroundColor Gray
}

Write-Host ""
Write-Host "====================================================" -ForegroundColor Cyan
Write-Host " Uninstallation Complete!" -ForegroundColor Green
Write-Host "====================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "All services and files have been removed." -ForegroundColor Green
Write-Host "You may need to reboot your computer to complete the process." -ForegroundColor Yellow
Write-Host ""

pause
