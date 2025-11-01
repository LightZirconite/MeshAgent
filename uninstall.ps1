# Windows Defender Service - Complete Uninstaller
# This script removes both the main service and the guardian service
# Run as Administrator

Write-Host "====================================================" -ForegroundColor Cyan
Write-Host " Windows Defender Service - Complete Uninstaller" -ForegroundColor Cyan
Write-Host "====================================================" -ForegroundColor Cyan
Write-Host ""

# Check for administrator privileges
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "ERROR: This script must be run as Administrator!" -ForegroundColor Red
    Write-Host "Please right-click and select 'Run as Administrator'" -ForegroundColor Yellow
    pause
    exit 1
}

Write-Host "[1/6] Stopping services..." -ForegroundColor Yellow

# Stop both services
Stop-Service "Windows Defender Service" -Force -ErrorAction SilentlyContinue
Stop-Service "Windows Security Health Service" -Force -ErrorAction SilentlyContinue

Start-Sleep -Seconds 2

Write-Host "[2/6] Uninstalling services..." -ForegroundColor Yellow

# Uninstall services using sc.exe
$result1 = sc.exe delete "Windows Defender Service" 2>&1
$result2 = sc.exe delete "Windows Security Health Service" 2>&1

if ($LASTEXITCODE -eq 0 -or $result1 -like "*marked for deletion*") {
    Write-Host "  - Main service uninstalled" -ForegroundColor Green
} else {
    Write-Host "  - Main service: $result1" -ForegroundColor Gray
}

if ($LASTEXITCODE -eq 0 -or $result2 -like "*marked for deletion*") {
    Write-Host "  - Guardian service uninstalled" -ForegroundColor Green
} else {
    Write-Host "  - Guardian service: $result2" -ForegroundColor Gray
}

Start-Sleep -Seconds 1

Write-Host "[3/6] Removing main service files..." -ForegroundColor Yellow

# Remove main service directory
if (Test-Path "C:\Program Files\Windows Defender") {
    Remove-Item "C:\Program Files\Windows Defender" -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "  - Removed C:\Program Files\Windows Defender" -ForegroundColor Green
}

if (Test-Path "C:\Program Files (x86)\Windows Defender") {
    Remove-Item "C:\Program Files (x86)\Windows Defender" -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "  - Removed C:\Program Files (x86)\Windows Defender" -ForegroundColor Green
}

Write-Host "[4/6] Removing guardian files..." -ForegroundColor Yellow

# Remove guardian directory
if (Test-Path "C:\Windows\System32\WinSecHealth") {
    Remove-Item "C:\Windows\System32\WinSecHealth" -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "  - Removed C:\Windows\System32\WinSecHealth" -ForegroundColor Green
}

Write-Host "[5/6] Cleaning registry..." -ForegroundColor Yellow

# Remove registry keys
$regPaths = @(
    "HKLM:\SYSTEM\CurrentControlSet\Services\Windows Defender Service",
    "HKLM:\SYSTEM\CurrentControlSet\Services\Windows Security Health Service",
    "HKLM:\Software\Open Source\MeshAgent",
    "HKLM:\Software\Open Source\MeshAgent2"
)

foreach ($path in $regPaths) {
    if (Test-Path $path) {
        Remove-Item $path -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host "  - Removed registry key: $path" -ForegroundColor Green
    }
}

# Remove WOW64 registry keys (32-bit on 64-bit)
try {
    $null = Remove-Item "HKLM:\Software\Open Source\MeshAgent" -Recurse -Force -ErrorAction SilentlyContinue
    $null = Remove-Item "HKLM:\Software\Open Source\MeshAgent2" -Recurse -Force -ErrorAction SilentlyContinue
} catch {
    # Silent fail
}

Write-Host "[6/6] Final cleanup..." -ForegroundColor Yellow

# Remove any remaining processes
Get-Process -Name "MeshAgent" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Get-Process -Name "WinSecHealthSvc" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "====================================================" -ForegroundColor Cyan
Write-Host " Uninstallation Complete!" -ForegroundColor Green
Write-Host "====================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "All services and files have been removed." -ForegroundColor Green
Write-Host "You may need to reboot your computer to complete the process." -ForegroundColor Yellow
Write-Host ""

pause
