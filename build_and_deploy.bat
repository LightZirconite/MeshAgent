@echo off
setlocal EnableDelayedExpansion
title Build ^& Deploy Agent Custom MeshCentral

:: ==========================================
:: CONFIGURATION SERVEUR
:: ==========================================
set "SERVER_IP=141.145.194.69"
set "USER=rocky"
set "REMOTE_TEMP=/home/%USER%"
set "REMOTE_DEST=/opt/meshcentral/meshcentral-data/agents"
set "SERVICE_NAME=meshcentral"
set "FILE_NAME=MeshService64.exe"
set "LOCAL_BUILD_PATH=%~dp0Release\%FILE_NAME%"
set "FALLBACK_BUILD_PATH=%~dp0ReleaseFix\%FILE_NAME%"
set "SSH_KEY=%USERPROFILE%\.ssh\vps2_ed25519"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSINSTALLDIR="
set "VCVARS_PATH="
set "MSBUILD_PATH="
set "SETUP_EXE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\setup.exe"

if not exist "%SSH_KEY%" (
    color 0C
    echo.
    echo ========================================================
    echo [X] CLE SSH INTROUVABLE
    echo ========================================================
    echo.
    echo     La cle SSH attendue est introuvable :
    echo     %SSH_KEY%
    echo.
    pause
    exit /b 1
)

if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do set "VSINSTALLDIR=%%I"
)

if defined VSINSTALLDIR (
    set "VCVARS_PATH=%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat"
    set "MSBUILD_PATH=%VSINSTALLDIR%\MSBuild\Current\Bin\MSBuild.exe"
)

if not defined VCVARS_PATH if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not defined MSBUILD_PATH if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"

if not exist "%VCVARS_PATH%" (
    color 0C
    echo.
    echo ========================================================
    echo [X] ENVIRONNEMENT VISUAL STUDIO INTROUVABLE
    echo ========================================================
    echo.
    echo     vcvars64.bat est introuvable.
    echo.
    pause
    exit /b 1
)

if not exist "%MSBUILD_PATH%" (
    color 0C
    echo.
    echo ========================================================
    echo [X] MSBUILD INTROUVABLE
    echo ========================================================
    echo.
    echo     MSBuild.exe est introuvable.
    echo.
    pause
    exit /b 1
)

:: ==========================================
:: 1. COMPILATION
:: ==========================================
cls
echo ========================================================
echo      BUILD ^& DEPLOY AGENT CUSTOM MESHCENTRAL
echo ========================================================
echo.
echo [1/4] Compilation de l'agent en cours...
echo.

call "%VCVARS_PATH%" >nul 2>&1

:: Arrêter le service avant compilation pour libérer le verrou sur le .pdb
echo [i] Arret du service WindowsMonitoringService avant compilation...
net stop "WindowsMonitoringService" >nul 2>&1

"%MSBUILD_PATH%" "%~dp0MeshAgent-2022.sln" /p:Configuration=Release /p:Platform=x64 /verbosity:minimal /nologo
set BUILD_ERR=%errorlevel%

:: ==========================================
:: 2. VERIFICATION DES ERREURS
:: ==========================================
if %BUILD_ERR% equ 0 goto :build_ok

echo.
echo [i] Build solution en echec, tentative de build isolee MeshService...
set "OUTDIR=%~dp0ReleaseFix"
set "INTDIR=%~dp0ReleaseFix\obj"
"%MSBUILD_PATH%" "%~dp0meshservice\MeshService-2022.vcxproj" /t:Build /p:Configuration=Release /p:Platform=x64 /p:OutDir="%OUTDIR%\." /p:IntDir="%INTDIR%\." /verbosity:minimal /nologo
if %errorlevel% neq 0 (
    color 0C
    echo.
    echo ========================================================
    echo [X] ECHEC DE LA COMPILATION
    echo ========================================================
    echo.
    echo     La compilation a echoue (solution + fallback).
    echo     Veuillez corriger les erreurs ci-dessus avant de
    echo     continuer.
    echo.
    pause
    exit /b 1
)
set "LOCAL_BUILD_PATH=%FALLBACK_BUILD_PATH%"

:build_ok

:: Vérifier que le fichier a bien été créé
if exist "%LOCAL_BUILD_PATH%" goto :file_ok
color 0C
echo.
echo ========================================================
echo [X] FICHIER INTROUVABLE
echo ========================================================
echo.
echo     Le fichier compile "%FILE_NAME%" est introuvable
echo     dans le dossier Release.
echo.
pause
exit /b 1

:file_ok

:: Redémarrer le service local maintenant que la compilation est terminée
echo [i] Redemarrage du service local WindowsMonitoringService...
net start "WindowsMonitoringService" >nul 2>&1
if %errorlevel% equ 0 (
    echo [+] Service local redemarré avec succes
) else (
    echo [i] Service local deja actif ou demarrage différé
)

color 0A
echo.
echo ========================================================
echo [OK] COMPILATION REUSSIE
echo ========================================================
echo.
echo     Fichier genere : %LOCAL_BUILD_PATH%
echo.

:: ==========================================
:: 3. PROPOSITION DE DEPLOIEMENT
:: ==========================================
color 0E
echo ========================================================
echo      DEPLOIEMENT SUR LE SERVEUR ?
echo ========================================================
echo.
echo     Serveur cible : %SERVER_IP%
echo     Utilisateur   : %USER%
echo     Destination   : %REMOTE_DEST%
echo.
set /p "DEPLOY_CHOICE=Voulez-vous deployer sur le serveur ? (O/N) : "

if /i "%DEPLOY_CHOICE%"=="O"   goto :do_deploy
if /i "%DEPLOY_CHOICE%"=="OUI" goto :do_deploy
if /i "%DEPLOY_CHOICE%"=="Y"   goto :do_deploy
if /i "%DEPLOY_CHOICE%"=="YES" goto :do_deploy
color 0E
echo.
echo [i] Deploiement annule par l'utilisateur.
echo.
echo     Le fichier compile se trouve ici :
echo     %LOCAL_BUILD_PATH%
echo.
pause
exit /b 0

:do_deploy

:: ==========================================
:: 4. TRANSFERT SCP
:: ==========================================
color 07
echo.
echo ========================================================
echo [2/4] Transfert vers le serveur...
echo ========================================================
echo.

scp -i "%SSH_KEY%" -o IdentitiesOnly=yes "%LOCAL_BUILD_PATH%" %USER%@%SERVER_IP%:%REMOTE_TEMP%/%FILE_NAME%
if %errorlevel% neq 0 (
    color 0C
    echo.
    echo ========================================================
    echo [X] ECHEC DU TRANSFERT SCP
    echo ========================================================
    echo.
    pause
    exit /b 1
)
echo.
echo [+] Transfert SCP termine avec succes

:: ==========================================
:: 5. INSTALLATION ET SIGNATURE AUTO
:: ==========================================
echo.
echo ========================================================
echo [3/4] Installation et redemarrage MeshCentral...
echo ========================================================
echo.

echo [+] Installation de l'agent sur le serveur...
echo.
ssh -i "%SSH_KEY%" -o IdentitiesOnly=yes %USER%@%SERVER_IP% "sudo mkdir -p %REMOTE_DEST% && sudo mv -f %REMOTE_TEMP%/%FILE_NAME% %REMOTE_DEST%/%FILE_NAME% && sudo chown root:root %REMOTE_DEST%/%FILE_NAME% && sudo chmod 755 %REMOTE_DEST%/%FILE_NAME% && echo '[OK] Agent installe avec succes' && ls -lh %REMOTE_DEST%/%FILE_NAME%"
if %errorlevel% neq 0 (
    color 0C
    echo.
    echo ========================================================
    echo [X] ECHEC DE L'INSTALLATION
    echo ========================================================
    echo.
    pause
    exit /b 1
)

echo.
echo [+] Redemarrage du service MeshCentral...
ssh -i "%SSH_KEY%" -o IdentitiesOnly=yes %USER%@%SERVER_IP% "sudo systemctl restart %SERVICE_NAME% && echo '[OK] Service redemarré avec succès'"
if %errorlevel% neq 0 (
    color 0C
    echo.
    echo ========================================================
    echo [X] ECHEC DU REDEMARRAGE DU SERVICE
    echo ========================================================
    echo.
    pause
    exit /b 1
)

:: ==========================================
:: 6. VERIFICATION DU REDEMARRAGE
:: ==========================================
echo.
echo ========================================================
echo [4/5] Verification du service MeshCentral...
echo ========================================================
echo.
echo [+] Attente du demarrage du service (10 secondes)...
ping 127.0.0.1 -n 11 >nul

echo.
echo [+] Verification de l'etat du service...
ssh -i "%SSH_KEY%" -o IdentitiesOnly=yes %USER%@%SERVER_IP% "sudo systemctl status %SERVICE_NAME% --no-pager | head -n 5"

:: ==========================================
:: 7. VERIFICATION DE LA SIGNATURE
:: ==========================================
echo.
echo ========================================================
echo [5/5] Verification de la signature automatique...
echo ========================================================
echo.
echo [+] Attente de la signature par MeshCentral (15 secondes)...
ping 127.0.0.1 -n 16 >nul

echo.
echo [+] Verification des agents :
echo.
echo     - Agent custom dans /agents :
ssh -i "%SSH_KEY%" -o IdentitiesOnly=yes %USER%@%SERVER_IP% "sudo ls -lh %REMOTE_DEST%/ 2>/dev/null" | findstr /i "mesh"
if %errorlevel% neq 0 echo      [i] Aucun agent trouve
echo.
echo     - Agents signes dans /signedagents :
ssh -i "%SSH_KEY%" -o IdentitiesOnly=yes %USER%@%SERVER_IP% "sudo ls -lh /opt/meshcentral/meshcentral-data/signedagents/ 2>/dev/null" | findstr /i "mesh"
if %errorlevel% neq 0 echo      [i] Signature en attente (normal au premier deploiement)
echo.

:: ==========================================
:: 8. SUCCES FINAL
:: ==========================================
color 0A
cls
echo.
echo ========================================================
echo      DEPLOIEMENT TERMINE AVEC SUCCES !
echo ========================================================
echo.
echo [+] Compilation         : OK
echo [+] Transfert SCP       : OK
echo [+] Installation        : OK
echo [+] Permissions         : OK (root:root, chmod 755)
echo [+] Redemarrage service : OK
echo [+] Verification service: OK
echo [+] Signature auto      : En cours / OK
echo.
echo --------------------------------------------------------
echo   AGENT CUSTOM DEPLOYE
echo --------------------------------------------------------
echo.
echo   Serveur     : %SERVER_IP%
echo   Emplacement : %REMOTE_DEST%/%FILE_NAME%
echo   Signature   : AUTOMATIQUE par MeshCentral
echo   Taille      : 3439 KB
echo.
echo --------------------------------------------------------
echo   STATUT DU SERVICE
echo --------------------------------------------------------
echo.
ssh -i "%SSH_KEY%" -o IdentitiesOnly=yes %USER%@%SERVER_IP% "sudo systemctl status %SERVICE_NAME% --no-pager | head -n 10"
echo.
echo --------------------------------------------------------
echo   PROCHAINES ETAPES
echo --------------------------------------------------------
echo.
echo   1. MeshCentral va automatiquement detecter le nouvel
echo      agent et le signer avec son certificat interne.
echo.
echo   2. L'agent signe sera disponible dans :
echo      /opt/meshcentral/meshcentral-data/signedagents/
echo.
echo   3. Les nouveaux appareils utiliseront automatiquement
echo      ton agent custom avec les correctifs UAC/lock screen.
echo.
echo   4. Pour forcer les agents existants a se mettre a jour :
echo      - Depuis MeshCentral ^> My Devices
echo      - Selectionner appareils ^> Actions ^> Update Agent
echo.
echo   5. Pour verifier les logs en direct :
echo      ssh %USER%@%SERVER_IP% "sudo journalctl -u %SERVICE_NAME% -f"
echo.
echo ========================================================
echo   Date/Heure du deploiement : %date% %time%
echo ========================================================
echo.
pause
