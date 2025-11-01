# Build and Deploy Script for MeshAgent
# This script automates the complete build and deployment process
# Usage: .\build-deploy.ps1 -All

param(
    [switch]$Build = $false,
    [switch]$Deploy = $false,
    [switch]$Test = $false,
    [switch]$All = $false,
    [string]$ServerIP = "141.145.194.69",
    [string]$ServerUser = "rocky",
    [string]$ServerPath = "/opt/meshcentral/meshagents",
    [string]$SSHKeyPath = "$env:USERPROFILE\.ssh\id_rsa"
)

$ErrorActionPreference = "Stop"

# Colors
function Write-Info { param($msg) Write-Host $msg -ForegroundColor Cyan }
function Write-Success { param($msg) Write-Host $msg -ForegroundColor Green }
function Write-Error { param($msg) Write-Host $msg -ForegroundColor Red }
function Write-Warning { param($msg) Write-Host $msg -ForegroundColor Yellow }

# Header
Write-Host ""
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "   MeshAgent Build & Deploy Script" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

# Check if running as Administrator
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin -and ($Test -or $All)) {
    Write-Error "Administrator privileges required for testing!"
    Write-Info "Please run this script as Administrator or use -Build/-Deploy only."
    exit 1
}

# Paths
$SolutionFile = "MeshAgent-2022.sln"
$GuardianProject = "meshguardian\MeshGuardian-2022.vcxproj"
$MeshServiceProject = "meshservice\MeshService-2022.vcxproj"
$OutputDir = ""
$MeshAgentExe = ""
$GuardianExe = ""
$Platform = "x64"
$Configuration = "Release"

# Build function
function Build-Solution {
    Write-Info "`n========================================="
    Write-Info "  STARTING BUILD PROCESS"
    Write-Info "=========================================`n"
    
    if (-not (Test-Path $SolutionFile)) {
        Write-Error "Solution file not found: $SolutionFile"
        exit 1
    }
    
    # Find MSBuild
    Write-Info "Looking for MSBuild..."
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    
    if (-not (Test-Path $vswhere)) {
        Write-Error "Visual Studio Installer not found. Please install Visual Studio 2022."
        exit 1
    }
    
    $msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
    
    if (-not $msbuild) {
        Write-Error "MSBuild not found. Please install Visual Studio 2022."
        exit 1
    }
    
    Write-Success "✓ Found MSBuild: $msbuild`n"
    
    # Set output paths based on configuration
    $script:OutputDir = "meshservice\$Platform\$Configuration"
    $script:GuardianExe = "meshguardian\$Platform\$Configuration\WinSecHealthSvc.exe"
    $script:MeshAgentExe = "$OutputDir\MeshAgent.exe"
    
    # Step 1: Clean solution
    Write-Info "[STEP 1/4] Cleaning solution..."
    & $msbuild $SolutionFile /t:Clean /p:Configuration=$Configuration /p:Platform=$Platform /v:minimal /nologo
    
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Clean failed with exit code $LASTEXITCODE"
        exit 1
    }
    Write-Success "✓ Clean complete`n"
    
    # Step 2: Build Guardian first (must be built before Agent)
    Write-Info "[STEP 2/4] Building Guardian service..."
    Write-Info "This must be built first as it will be embedded in the Agent..."
    
    & $msbuild $GuardianProject /t:Build /p:Configuration=$Configuration /p:Platform=$Platform /v:minimal /nologo
    
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Guardian build failed with exit code $LASTEXITCODE"
        exit 1
    }
    
    if (-not (Test-Path $GuardianExe)) {
        Write-Error "Guardian executable not found after build: $GuardianExe"
        exit 1
    }
    
    $guardianSize = (Get-Item $GuardianExe).Length
    Write-Success "✓ Guardian built successfully!"
    Write-Info "  Location: $GuardianExe"
    Write-Info "  Size: $guardianSize bytes`n"
    
    # Step 3: Embed Guardian into Agent resources
    Write-Info "[STEP 3/4] Embedding Guardian into Agent resources..."
    
    # The Guardian should be embedded as resource ID 200 type "GUARDIAN_EXE"
    # This is typically done via the .rc file, but we need to ensure it's there
    $rcFile = "meshservice\MeshService.rc"
    
    if (Test-Path $rcFile) {
        $rcContent = Get-Content $rcFile -Raw
        
        # Check if Guardian resource is defined
        if ($rcContent -notmatch "GUARDIAN_EXE") {
            Write-Warning "Guardian resource not found in .rc file. Adding it..."
            
            # Add Guardian resource
            $guardianResourcePath = "..\meshguardian\$Platform\$Configuration\WinSecHealthSvc.exe"
            $resourceLine = "`r`n200 GUARDIAN_EXE `"$guardianResourcePath`""
            Add-Content -Path $rcFile -Value $resourceLine
            
            Write-Success "✓ Guardian resource added to .rc file"
        } else {
            Write-Info "✓ Guardian resource already defined in .rc file"
        }
    }
    
    Write-Success "✓ Resource preparation complete`n"
    
    # Step 4: Build Agent (will include Guardian as embedded resource)
    Write-Info "[STEP 4/4] Building Agent with embedded Guardian..."
    
    & $msbuild $MeshServiceProject /t:Build /p:Configuration=$Configuration /p:Platform=$Platform /v:minimal /nologo
    
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Agent build failed with exit code $LASTEXITCODE"
        exit 1
    }
    
    # Agent build: determine actual built file and copy to MeshAgent.exe (single-file deliverable)
    $builtAgentPath = "meshservice\Release\MeshService64.exe"
    if (-not (Test-Path $builtAgentPath)) {
        # Some setups may output directly MeshAgent.exe; check for that as fallback
        $fallback = "meshservice\Release\MeshAgent.exe"
        if (-not (Test-Path $fallback)) {
            Write-Error "Agent executable not found after build: $builtAgentPath or $fallback"
            exit 1
        } else {
            $script:MeshAgentExe = $fallback
        }
    } else {
        # Copy the built output to the single-file name MeshAgent.exe
        Copy-Item $builtAgentPath "meshservice\Release\MeshAgent.exe" -Force
        $script:MeshAgentExe = "meshservice\Release\MeshAgent.exe"
    }

    $agentSize = (Get-Item $script:MeshAgentExe).Length
    Write-Success "✓ Agent built successfully!"
    Write-Info "  Location: $script:MeshAgentExe"
    Write-Info "  Size: $agentSize bytes"
    Write-Info "  Includes: Embedded Guardian service`n"
    
    # Verify the Guardian is embedded
    Write-Info "Verifying Guardian is embedded in Agent..."
    
    # Simple check: Agent should be larger than before (contains Guardian)
    $expectedMinSize = $guardianSize + 1000000  # Guardian + at least 1MB for agent code
    
    if ($agentSize -ge $expectedMinSize) {
        Write-Success "✓ Guardian appears to be properly embedded (Agent size: $agentSize bytes)`n"
    } else {
        Write-Warning "! Agent size seems small. Guardian may not be embedded correctly."
        Write-Warning "  Expected at least: $expectedMinSize bytes"
        Write-Warning "  Actual: $agentSize bytes"
        Write-Info "  Continuing anyway...`n"
    }
    
    Write-Success "========================================="
    Write-Success "  BUILD COMPLETED SUCCESSFULLY!"
    Write-Success "=========================================`n"
    
    Write-Info "Output files:"
    Write-Info "  Agent:    $MeshAgentExe ($agentSize bytes)"
    Write-Info "  Guardian: $GuardianExe ($guardianSize bytes)"
}

# Deploy function
function Deploy-ToServer {
    Write-Info "`n========================================="
    Write-Info "  STARTING DEPLOYMENT TO SERVER"
    Write-Info "=========================================`n"
    
    if (-not (Test-Path $MeshAgentExe)) {
        Write-Error "MeshAgent.exe not found: $MeshAgentExe"
        Write-Warning "Please run with -Build first."
        exit 1
    }
    
    # Check if ssh/scp are available
    Write-Info "Checking SSH tools..."
    
    $sshCmd = Get-Command ssh -ErrorAction SilentlyContinue
    $scpCmd = Get-Command scp -ErrorAction SilentlyContinue
    
    if (-not $sshCmd -or -not $scpCmd) {
        Write-Error "SSH/SCP commands not found. Please install OpenSSH client."
        Write-Info "You can install it via: Settings → Apps → Optional Features → OpenSSH Client"
        exit 1
    }
    
    Write-Success "✓ SSH tools found`n"
    
    # Test SSH connection
    Write-Info "Testing SSH connection to $ServerUser@$ServerIP..."
    
    $testConnection = ssh -o ConnectTimeout=5 -o BatchMode=yes "$ServerUser@$ServerIP" "echo OK" 2>&1
    
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "! SSH connection test failed"
        Write-Info "Attempting connection with password prompt..."
        Write-Info ""
    } else {
        Write-Success "✓ SSH connection successful`n"
    }
    
    # Create backup on server
    Write-Info "[STEP 1/3] Backing up current agent on server..."
    
    $backupCmd = "cd $ServerPath && if [ -f MeshAgent-signed.exe ]; then cp MeshAgent-signed.exe MeshAgent-signed.exe.backup && echo 'Backup created'; else echo 'No existing agent to backup'; fi"
    
    $backupResult = ssh "$ServerUser@$ServerIP" $backupCmd 2>&1
    
    if ($LASTEXITCODE -eq 0) {
        Write-Success "✓ $backupResult`n"
    } else {
        Write-Warning "! Backup may have failed: $backupResult`n"
    }
    
    # Upload new agent
    Write-Info "[STEP 2/3] Uploading MeshAgent.exe to server..."
    Write-Info "  From: $MeshAgentExe"
    Write-Info "  To:   $ServerUser@${ServerIP}:$ServerPath/MeshAgent-signed.exe"
    
    $localSize = (Get-Item $MeshAgentExe).Length
    Write-Info "  Size: $localSize bytes`n"
    
    scp $MeshAgentExe "$ServerUser@${ServerIP}:$ServerPath/MeshAgent-signed.exe"
    
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Upload failed with exit code $LASTEXITCODE"
        Write-Info "Attempting to restore backup..."
        ssh "$ServerUser@$ServerIP" "cd $ServerPath && mv MeshAgent-signed.exe.backup MeshAgent-signed.exe"
        exit 1
    }
    
    Write-Success "✓ Upload complete`n"
    
    # Verify upload
    Write-Info "[STEP 3/3] Verifying uploaded file..."
    
    $remoteSize = ssh "$ServerUser@$ServerIP" "stat -c%s $ServerPath/MeshAgent-signed.exe 2>/dev/null || stat -f%z $ServerPath/MeshAgent-signed.exe 2>/dev/null"
    
    if ($remoteSize -eq $localSize) {
        Write-Success "✓ File verification successful!"
        Write-Info "  Local size:  $localSize bytes"
        Write-Info "  Remote size: $remoteSize bytes`n"
    } else {
        Write-Error "File size mismatch!"
        Write-Info "  Local size:  $localSize bytes"
        Write-Info "  Remote size: $remoteSize bytes"
        Write-Warning "Upload may be corrupted. Please check manually."
        exit 1
    }
    
    # Set permissions
    Write-Info "Setting file permissions..."
    ssh "$ServerUser@$ServerIP" "chmod +x $ServerPath/MeshAgent-signed.exe"
    Write-Success "✓ Permissions set`n"
    
    Write-Success "========================================="
    Write-Success "  DEPLOYMENT COMPLETED SUCCESSFULLY!"
    Write-Success "=========================================`n"
    
    Write-Info "Deployment summary:"
    Write-Info "  Server:   $ServerUser@$ServerIP"
    Write-Info "  Path:     $ServerPath/MeshAgent-signed.exe"
    Write-Info "  Size:     $localSize bytes"
    Write-Info "  Backup:   $ServerPath/MeshAgent-signed.exe.backup"
    Write-Info ""
    Write-Info "The agent is now available for download from your MeshCentral server."
}

# Test function
function Test-Installation {
    Write-Info "`n========================================="
    Write-Info "  TESTING INSTALLATION"
    Write-Info "=========================================`n"
    
    $allPassed = $true
    
    # Check main service
    Write-Info "[TEST 1/5] Checking Windows Defender Service..."
    $mainService = Get-Service "Windows Defender Service" -ErrorAction SilentlyContinue
    
    if ($mainService) {
        if ($mainService.Status -eq "Running") {
            Write-Success "✓ Main service is running"
        } else {
            Write-Warning "! Main service exists but is not running: $($mainService.Status)"
            $allPassed = $false
        }
    } else {
        Write-Info "○ Main service not found (not yet installed)"
    }
    
    # Check Guardian service
    Write-Info "`n[TEST 2/5] Checking Windows Security Health Service..."
    $guardianService = Get-Service "Windows Security Health Service" -ErrorAction SilentlyContinue
    
    if ($guardianService) {
        if ($guardianService.Status -eq "Running") {
            Write-Success "✓ Guardian service is running"
        } else {
            Write-Warning "! Guardian service exists but is not running: $($guardianService.Status)"
            $allPassed = $false
        }
    } else {
        Write-Info "○ Guardian service not found (not yet installed)"
    }
    
    # Check paths
    Write-Info "`n[TEST 3/5] Checking installation paths..."
    
    $mainPath = "C:\Program Files\Windows Security\MeshAgent.exe"
    if (Test-Path $mainPath) {
        $size = (Get-Item $mainPath).Length
        Write-Success "✓ Main service executable found: $mainPath ($size bytes)"
    } else {
        Write-Info "○ Main service executable not found: $mainPath"
    }
    
    $guardianPath = "C:\Windows\System32\WinSecHealth\WinSecHealthSvc.exe"
    if (Test-Path $guardianPath) {
        $size = (Get-Item $guardianPath).Length
        Write-Success "✓ Guardian executable found: $guardianPath ($size bytes)"
    } else {
        Write-Info "○ Guardian executable not found: $guardianPath"
    }
    
    # Check Guardian log
    Write-Info "`n[TEST 4/5] Checking Guardian log..."
    $logPath = "C:\Windows\System32\WinSecHealth\service.log"
    
    if (Test-Path $logPath) {
        $logSize = (Get-Item $logPath).Length
        Write-Success "✓ Guardian log file found ($logSize bytes)"
        
        Write-Info "`nLast 10 lines of Guardian log:"
        Write-Info "---------------------------------------------"
        Get-Content $logPath -Tail 10 -ErrorAction SilentlyContinue | ForEach-Object { 
            Write-Host "  $_" -ForegroundColor Gray 
        }
        Write-Info "---------------------------------------------"
    } else {
        Write-Info "○ Guardian log file not found: $logPath"
    }
    
    # Check backup directory
    Write-Info "`n[TEST 5/5] Checking Guardian backup..."
    $backupDir = "C:\Windows\System32\WinSecHealth\backup"
    
    if (Test-Path $backupDir) {
        $backupFiles = Get-ChildItem $backupDir -ErrorAction SilentlyContinue
        if ($backupFiles) {
            Write-Success "✓ Guardian backup directory found with $($backupFiles.Count) files"
            $backupFiles | ForEach-Object {
                Write-Info "  - $($_.Name) ($($_.Length) bytes)"
            }
        } else {
            Write-Info "○ Guardian backup directory exists but is empty"
        }
    } else {
        Write-Info "○ Guardian backup directory not found: $backupDir"
    }
    
    # Summary
    Write-Info ""
    Write-Info "========================================="
    
    if ($mainService -and $guardianService -and $mainService.Status -eq "Running" -and $guardianService.Status -eq "Running") {
        Write-Success "  ALL TESTS PASSED!"
        Write-Success "  System is fully operational"
    } elseif ($mainService -or $guardianService) {
        Write-Warning "  PARTIAL INSTALLATION DETECTED"
        Write-Info "  Some services are present but not all running"
    } else {
        Write-Info "  NO INSTALLATION DETECTED"
        Write-Info "  Services have not been installed yet"
    }
    
    Write-Info "=========================================`n"
}

# Main execution
try {
    Write-Host ""
    Write-Host "╔════════════════════════════════════════════════════╗" -ForegroundColor Cyan
    Write-Host "║                                                    ║" -ForegroundColor Cyan
    Write-Host "║        MeshAgent Build & Deploy System             ║" -ForegroundColor Cyan
    Write-Host "║                                                    ║" -ForegroundColor Cyan
    Write-Host "╚════════════════════════════════════════════════════╝" -ForegroundColor Cyan
    Write-Host ""
    
    if ($All) {
        Write-Info "Running FULL BUILD AND DEPLOYMENT process...`n"
        Build-Solution
        Write-Host ""
        Deploy-ToServer
        Write-Host ""
        
        if ($isAdmin) {
            Test-Installation
        } else {
            Write-Info "Skipping tests (requires Administrator privileges)"
        }
    } else {
        if ($Build) { 
            Build-Solution 
            Write-Host ""
        }
        if ($Deploy) { 
            Deploy-ToServer 
            Write-Host ""
        }
        if ($Test) { 
            Test-Installation 
            Write-Host ""
        }
        
        if (-not ($Build -or $Deploy -or $Test)) {
            Write-Info "╔════════════════════════════════════════════════════╗"
            Write-Info "║                 USAGE INSTRUCTIONS                 ║"
            Write-Info "╚════════════════════════════════════════════════════╝`n"
            
            Write-Info "Basic Usage:"
            Write-Info "  .\build-deploy.ps1 -All              # Recommended: Build, Deploy & Test"
            Write-Info ""
            Write-Info "Individual Steps:"
            Write-Info "  .\build-deploy.ps1 -Build            # Build only"
            Write-Info "  .\build-deploy.ps1 -Deploy           # Deploy only"
            Write-Info "  .\build-deploy.ps1 -Test             # Test only (requires Admin)"
            Write-Info ""
            Write-Info "Combined:"
            Write-Info "  .\build-deploy.ps1 -Build -Deploy   # Build and deploy"
            Write-Info ""
            Write-Info "Optional Parameters:"
            Write-Info "  -ServerIP <ip>       # Server IP (default: 141.145.194.69)"
            Write-Info "  -ServerUser <user>   # SSH user (default: rocky)"
            Write-Info "  -ServerPath <path>   # Deploy path (default: /opt/meshcentral/meshagents)"
            Write-Info "  -Platform <arch>     # Build platform (default: x64)"
            Write-Info "  -Configuration <cfg> # Build config (default: Release)"
            Write-Info ""
            Write-Info "Examples:"
            Write-Info "  .\build-deploy.ps1 -All"
            Write-Info "  .\build-deploy.ps1 -Build -Deploy"
            Write-Info "  .\build-deploy.ps1 -Deploy -ServerIP 192.168.1.100"
            Write-Info ""
            Write-Warning "⚠ No action specified. Please use -Build, -Deploy, -Test, or -All"
            Write-Info ""
            exit 0
        }
    }
    
    Write-Host ""
    Write-Success "╔════════════════════════════════════════════════════╗"
    Write-Success "║                                                    ║"
    Write-Success "║              OPERATION COMPLETED!                  ║"
    Write-Success "║                                                    ║"
    Write-Success "╚════════════════════════════════════════════════════╝"
    Write-Host ""
    
} catch {
    Write-Host ""
    Write-Error "╔════════════════════════════════════════════════════╗"
    Write-Error "║                                                    ║"
    Write-Error "║                  ERROR OCCURRED                    ║"
    Write-Error "║                                                    ║"
    Write-Error "╚════════════════════════════════════════════════════╝"
    Write-Host ""
    Write-Error "Error: $_"
    Write-Info "Stack trace:"
    Write-Info $_.ScriptStackTrace
    Write-Host ""
    exit 1
}
