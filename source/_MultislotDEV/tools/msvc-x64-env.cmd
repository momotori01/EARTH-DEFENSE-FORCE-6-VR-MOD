@echo off
rem Call this script from cmd.exe to initialize an x64 MSVC build environment.
rem Do not setlocal: the calling build script needs these environment variables.
set "MULTISLOT_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%MULTISLOT_VSWHERE%" (
    echo Visual Studio Installer / vswhere.exe was not found. 1>&2
    exit /b 1
)
set "MULTISLOT_VSROOT="
for /f "usebackq tokens=*" %%I in (`"%MULTISLOT_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "MULTISLOT_VSROOT=%%I"
if not defined MULTISLOT_VSROOT (
    echo No Visual Studio installation with the C++ x64 tools was found. 1>&2
    exit /b 1
)
call "%MULTISLOT_VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
set "MULTISLOT_CMAKE_ROOT=%MULTISLOT_VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake"
if exist "%MULTISLOT_CMAKE_ROOT%\CMake\bin\cmake.exe" set "PATH=%MULTISLOT_CMAKE_ROOT%\CMake\bin;%PATH%"
if exist "%MULTISLOT_CMAKE_ROOT%\Ninja\ninja.exe" set "PATH=%MULTISLOT_CMAKE_ROOT%\Ninja;%PATH%"
exit /b 0
