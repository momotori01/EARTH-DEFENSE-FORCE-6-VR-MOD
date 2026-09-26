@echo off
setlocal
call "%~dp0msvc-x64-env.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
ml64 /nologo /c /Fosmoke-asm.obj smoke.asm
if errorlevel 1 exit /b 1
cl /nologo /W4 /EHsc /std:c++17 /MT /Fosmoke-cpp.obj /Fesmoke.exe smoke.cpp smoke-asm.obj /link kernel32.lib
if errorlevel 1 exit /b 1
smoke.exe
if errorlevel 1 exit /b 1
cmake --version
if errorlevel 1 exit /b 1
ninja --version
exit /b %errorlevel%
