@echo off
setlocal
call "%~dp0tools\msvc-x64-env.cmd"
if errorlevel 1 exit /b 1
set VSLANG=1033
cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
if errorlevel 1 exit /b 1
cmake --build "%~dp0build"
if errorlevel 1 exit /b 1
ctest --test-dir "%~dp0build" --output-on-failure
exit /b %errorlevel%
