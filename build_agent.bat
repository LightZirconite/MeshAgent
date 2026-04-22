@echo off
setlocal
title Build MeshAgent

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSINSTALLDIR="
set "VCVARS_PATH="
set "MSBUILD_PATH="
set "RELEASE_DIR=%~dp0Release"
set "MESH_SERVICE_PDB=%RELEASE_DIR%\MeshService64.pdb"

if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do set "VSINSTALLDIR=%%I"
)

if defined VSINSTALLDIR (
    set "VCVARS_PATH=%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat"
    set "MSBUILD_PATH=%VSINSTALLDIR%\MSBuild\Current\Bin\MSBuild.exe"
)

if not defined VCVARS_PATH if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS_PATH=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if not defined MSBUILD_PATH if exist "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD_PATH=C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"

if not defined VCVARS_PATH if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not defined MSBUILD_PATH if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"

if not exist "%VCVARS_PATH%" (
    color 0C
    echo [X] vcvars64.bat introuvable.
    echo.
    pause
    exit /b 1
)

if not exist "%MSBUILD_PATH%" (
    color 0C
    echo [X] MSBuild.exe introuvable.
    echo.
    pause
    exit /b 1
)

call "%VCVARS_PATH%"
if errorlevel 1 (
    color 0E
    echo [!] Initialisation Visual Studio signalee avec avertissement. Le build continue avec l'environnement disponible.
    echo.
)

if exist "%MESH_SERVICE_PDB%" (
    del /f /q "%MESH_SERVICE_PDB%" >nul 2>nul
    if exist "%MESH_SERVICE_PDB%" (
        color 0E
        echo [!] Impossible de supprimer MeshService64.pdb avant le build. Le build continue quand meme.
        echo.
    ) else (
        echo [i] Ancien MeshService64.pdb supprime avant le build.
    )
)

"%MSBUILD_PATH%" MeshAgent-2022.sln /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /verbosity:minimal
set "BUILD_ERR=%errorlevel%"

if not "%BUILD_ERR%"=="0" (
    color 0C
    echo.
    echo [X] Le build a echoue. Code erreur: %BUILD_ERR%
    echo [i] Si un fichier .pdb est verrouille, ferme MeshService64.exe, MeshConsole64.exe ou tout process qui tient le binaire ouvert, puis relance le build.
    echo.
    pause
    exit /b %BUILD_ERR%
)

color 0A
echo.
echo [OK] Build termine avec succes.
endlocal
