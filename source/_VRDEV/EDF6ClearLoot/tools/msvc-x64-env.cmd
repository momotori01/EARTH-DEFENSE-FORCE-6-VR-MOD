@echo off
rem Call this script from cmd.exe to initialize an x64 MSVC build environment.
rem Do not setlocal: the calling build script needs these environment variables.
set "EDF6VR_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%EDF6VR_VSWHERE%" (
    echo Visual Studio Installer / vswhere.exe was not found. 1>&2
    exit /b 1
)
set "EDF6VR_VSROOT="
for /f "usebackq tokens=*" %%I in (`"%EDF6VR_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "EDF6VR_VSROOT=%%I"
if not defined EDF6VR_VSROOT (
    echo No Visual Studio installation with the C++ x64 tools was found. 1>&2
    exit /b 1
)
call "%EDF6VR_VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
set "EDF6VR_CMAKE_ROOT=%EDF6VR_VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake"
if exist "%EDF6VR_CMAKE_ROOT%\CMake\bin\cmake.exe" set "PATH=%EDF6VR_CMAKE_ROOT%\CMake\bin;%PATH%"
if exist "%EDF6VR_CMAKE_ROOT%\Ninja\ninja.exe" set "PATH=%EDF6VR_CMAKE_ROOT%\Ninja;%PATH%"
exit /b 0
