@echo off
rem Call this script from cmd.exe to initialize an x64 MSVC build environment.
rem Do not setlocal: the calling build script needs these environment variables.
set "EDF6VR_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%EDF6VR_VSWHERE%" (
    echo Visual Studio Installer / vswhere.exe was not found. 1>&2
    exit /b 1
)
set "EDF6VR_VSROOT="
rem Visual Studio 2019 first: every release was built and tested with it, and a build
rem folder configured with its compiler cannot take a newer one's headers. Else the latest.
for /f "usebackq tokens=*" %%I in (`"%EDF6VR_VSWHERE%" -version [16.0^,17.0^) -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "EDF6VR_VSROOT=%%I"
if not defined EDF6VR_VSROOT for /f "usebackq tokens=*" %%I in (`"%EDF6VR_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "EDF6VR_VSROOT=%%I"
if not defined EDF6VR_VSROOT (
    echo No Visual Studio installation with the C++ x64 tools was found. 1>&2
    exit /b 1
)
rem Windows SDK 10.0.19041.0 when it is there: the cockpit occlusion bakes are keyed to the
rem cockpit meshes bit for bit, and the static CRT's math in SDK 10.0.26100.0 builds them
rem differently (2026-11-03: every bake refused). vcvars would take the newest SDK.
set "EDF6VR_WINSDK="
if exist "%ProgramFiles(x86)%\Windows Kits\10\Lib\10.0.19041.0\ucrt\x64\libucrt.lib" set "EDF6VR_WINSDK=10.0.19041.0"
call "%EDF6VR_VSROOT%\VC\Auxiliary\Build\vcvars64.bat" %EDF6VR_WINSDK%
if errorlevel 1 exit /b 1
set "EDF6VR_CMAKE_ROOT=%EDF6VR_VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake"
if exist "%EDF6VR_CMAKE_ROOT%\CMake\bin\cmake.exe" set "PATH=%EDF6VR_CMAKE_ROOT%\CMake\bin;%PATH%"
if exist "%EDF6VR_CMAKE_ROOT%\Ninja\ninja.exe" set "PATH=%EDF6VR_CMAKE_ROOT%\Ninja;%PATH%"
exit /b 0
