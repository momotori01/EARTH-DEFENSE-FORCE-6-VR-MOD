@echo off
if not exist "%~dp0EDF6.exe" (echo Put Update_EDF6VR.bat in the folder containing EDF6.exe.& pause & exit /b 1)
rem One line on purpose: the update may replace this file while it runs.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0EDF6VR\Update-EDF6VR.ps1" & pause & exit /b
