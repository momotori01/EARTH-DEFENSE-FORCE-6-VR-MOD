@echo off
rem Call this script from cmd.exe to initialize an x64 MSVC build environment.
rem Do not setlocal: the calling build script needs these environment variables.
set "MULTISLOT_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%MULTISLOT_VSWHERE%" (
    echo Visual Studio Installer / vswhere.exe was not found. 1>&2
    exit /b 1
)
set "MULTISLOT_VSROOT="
rem Visual Studio 2019 first: every release was built and tested with it, and a build
rem folder configured with its compiler cannot take a newer one's headers. Else the latest.
for /f "usebackq tokens=*" %%I in (`"%MULTISLOT_VSWHERE%" -version [16.0^,17.0^) -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "MULTISLOT_VSROOT=%%I"
if not defined MULTISLOT_VSROOT for /f "usebackq tokens=*" %%I in (`"%MULTISLOT_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "MULTISLOT_VSROOT=%%I"
if not defined MULTISLOT_VSROOT (
    echo No Visual Studio installation with the C++ x64 tools was found. 1>&2
    exit /b 1
)
rem Windows SDK 10.0.19041.0 when it is there, as EDF6VR's build does: vcvars would take the
rem newest (10.0.26100.0 since 2026-11-02), whose static CRT gives different float results.
set "MULTISLOT_WINSDK="
if exist "%ProgramFiles(x86)%\Windows Kits\10\Lib\10.0.19041.0\ucrt\x64\libucrt.lib" set "MULTISLOT_WINSDK=10.0.19041.0"
call "%MULTISLOT_VSROOT%\VC\Auxiliary\Build\vcvars64.bat" %MULTISLOT_WINSDK%
if errorlevel 1 exit /b 1
set "MULTISLOT_CMAKE_ROOT=%MULTISLOT_VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake"
if exist "%MULTISLOT_CMAKE_ROOT%\CMake\bin\cmake.exe" set "PATH=%MULTISLOT_CMAKE_ROOT%\CMake\bin;%PATH%"
if exist "%MULTISLOT_CMAKE_ROOT%\Ninja\ninja.exe" set "PATH=%MULTISLOT_CMAKE_ROOT%\Ninja;%PATH%"
exit /b 0
