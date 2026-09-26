@echo off
setlocal DisableDelayedExpansion
if not exist "%~dp0EDF6.exe" (
  echo Put VR_Play.bat in the folder containing EDF6.exe.
  pause
  exit /b 1
)
choice /C YN /N /M "VR Play? Y/N "
if errorlevel 255 exit /b 1
if errorlevel 2 goto Flat
if errorlevel 1 goto VR
exit /b 1
:VR
set "EDFMODE=VR"
goto Run
:Flat
set "EDFMODE=Flat"
:Run
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0EDF6VR\Switch-VR.ps1" -Mode %EDFMODE%
if errorlevel 1 (
  echo Could not switch mode. See the message above.
  pause
  exit /b 1
)
pause
endlocal
