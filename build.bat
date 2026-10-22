@echo off
rem Builds Steam Achievement Manager with Visual Studio 2026 and puts a
rem portable copy (executables + Qt runtime) into the "release" folder.
rem
rem   build.bat            Release build into .\release
rem   build.bat debug      Debug build into .\release-debug
rem
rem Qt is taken from QT6_HOME_64 (e.g. C:\Qt\6.10.0\msvc2022_64).

setlocal EnableExtensions
cd /d "%~dp0"

set "CONFIG=release"
set "OUTPUT=%~dp0release"
if /i "%~1"=="debug" (
    set "CONFIG=debug"
    set "OUTPUT=%~dp0release-debug"
)

if not defined QT6_HOME_64 (
    echo [ERROR] QT6_HOME_64 is not set. Point it to the Qt kit, e.g.:
    echo         set QT6_HOME_64=C:\Qt\6.10.0\msvc2022_64
    exit /b 1
)
if not exist "%QT6_HOME_64%\bin\qmake.exe" if not exist "%QT6_HOME_64%\bin\qtpaths.exe" (
    echo [ERROR] No Qt kit found in "%QT6_HOME_64%".
    exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe not found; is Visual Studio installed?
    exit /b 1
)
set "VSDIR="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -version [18.0^,19.0^) -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo [ERROR] Visual Studio 2026 with the C++ workload was not found.
    exit /b 1
)

echo === Visual Studio: %VSDIR%
echo === Qt:            %QT6_HOME_64%
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

echo === Configure (%CONFIG%)
cmake --preset %CONFIG% || goto :failed

echo === Build
cmake --build --preset %CONFIG% || goto :failed

echo === Deploy to %OUTPUT%
if exist "%OUTPUT%" (
    rmdir /s /q "%OUTPUT%" 2>nul
    rem The folder itself may be held open (e.g. by Explorer); only leftover files matter.
    if exist "%OUTPUT%" for /f %%f in ('dir /b /a "%OUTPUT%" 2^>nul') do (
        echo [ERROR] Cannot clean "%OUTPUT%". Close SAM.Picker / SAM.Game and try again.
        exit /b 1
    )
)
cmake --install build\%CONFIG% --prefix "%OUTPUT%" || goto :failed

echo.
echo === Done: %OUTPUT%
exit /b 0

:failed
echo.
echo [ERROR] Build failed.
exit /b 1
