@echo off
setlocal
cd /d "%~dp0"

set "INI=Mods\Plugins\EDF6VR.ini"
set "PY=Mods\HDTexture\python\python.exe"
set "TOOL=Mods\HDTexture\set_resolution.py"

rem The settings file is made by the mod on first start. Before that, start
rem from the shipped defaults so the size can be chosen first.
if not exist "%INI%" if exist "EDF6VR\EDF6VR.defaults.ini" copy /y "EDF6VR\EDF6VR.defaults.ini" "%INI%" >nul
if not exist "%INI%" goto nofiles
if not exist "%PY%" goto nofiles

:menu
cls
echo ==================================================
echo    EDF6 VR  -  Picture Size
echo ==================================================
echo.
echo   A bigger picture looks sharper.
echo   A bigger picture also gives lower FPS.
echo.
echo   The number is how many times the normal size.
echo   1.0 is normal.
echo.
echo     1)  Low       0.8    more FPS
echo     2)  Normal    1.0    recommended
echo     3)  High      1.25   sharper
echo     4)  Custom    you type the number
echo.
set "PICK="
set /p "PICK=  Type 1, 2, 3 or 4 and press Enter: "
if "%PICK%"=="1" set "SCALE=0.8" & goto apply
if "%PICK%"=="2" set "SCALE=1.0" & goto apply
if "%PICK%"=="3" set "SCALE=1.25" & goto apply
if "%PICK%"=="4" goto custom
goto menu

:custom
echo.
echo   Type a number. 1.0 is the normal size.
echo   0.9 is smaller. 1.5 is bigger.
echo   The biggest you can use is 2.0.
echo.
set "SCALE="
set /p "SCALE=  Number: "
if "%SCALE%"=="" goto menu

:apply
"%PY%" "%TOOL%" "%INI%" "%SCALE%"
echo.
pause
exit /b 0

:nofiles
echo.
echo   This file must sit in your
echo   EARTH DEFENSE FORCE 6 folder,
echo   next to EDF6.exe.
echo.
pause
exit /b 1
