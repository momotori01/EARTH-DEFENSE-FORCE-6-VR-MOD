@echo off
rem The warp shader is compiled at runtime, so a mistake in it does not fail the
rem build: it fails in the headset, as a line in the log. This compiles every
rem entry point here instead. Skipped without complaint where fxc is not around,
rem since it is a check and not a build step.
setlocal enabledelayedexpansion
set FXC=
rem dir does not glob a middle directory, so the search has to recurse.
for /f "delims=" %%f in ('where /r "%ProgramFiles(x86)%\Windows Kits\10\bin" fxc.exe 2^>nul ^| findstr /i x64') do set FXC=%%f
if not defined FXC (
    echo shader check skipped: fxc.exe not found
    exit /b 0
)
python "%~dp0extract_shader.py" "%TEMP%\edf6vr_eye_warp.hlsl" >nul
if errorlevel 1 exit /b 1
set FAILED=0
for %%e in (PsMain PsUi PsErase PsMaskRaw PsMaskErode PsMaskGrow) do (
    "!FXC!" /nologo /T ps_5_0 /E %%e /O3 /Fo NUL "%TEMP%\edf6vr_eye_warp.hlsl" || set FAILED=1
)
"!FXC!" /nologo /T vs_5_0 /E VsMain /O3 /Fo NUL "%TEMP%\edf6vr_eye_warp.hlsl" || set FAILED=1
python "%~dp0extract_shader.py" "%TEMP%\edf6vr_cockpit.hlsl" "%~dp0..\src\cockpit_draw.cpp" >nul
if errorlevel 1 exit /b 1
"!FXC!" /nologo /T vs_5_0 /E vertex /O3 /Fo NUL "%TEMP%\edf6vr_cockpit.hlsl" || set FAILED=1
"!FXC!" /nologo /T ps_5_0 /E fragment /O3 /Fo NUL "%TEMP%\edf6vr_cockpit.hlsl" || set FAILED=1
"!FXC!" /nologo /T cs_5_0 /E crop /O3 /Fo NUL "%TEMP%\edf6vr_cockpit.hlsl" || set FAILED=1
if "!FAILED!"=="1" (
    echo shader check FAILED
    exit /b 1
)
echo shader check passed
exit /b 0
