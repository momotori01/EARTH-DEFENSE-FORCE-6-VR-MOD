@echo off
setlocal
cd /d "%~dp0"

set "PY=Mods\HDTexture\python\python.exe"
set "TOOL=Mods\HDTexture\hd_textures.py"

if not exist "EDF6.exe" goto nofiles
if not exist "%PY%" goto nofiles

:menu
cls
echo ==================================================
echo    EDF6 VR  -  HD Textures  (2x)
echo ==================================================
echo.
echo   This makes the pictures on things 2 times
echo   sharper. Walls, roads, signs, shop fronts,
echo   weapons and enemies.
echo.
echo   It reads your own game files and writes new
echo   ones into the Mods folder.
echo   Your game files are never changed.
echo.
echo   It needs about 40 GB of free disk space.
echo   In the game it uses about 300 MB more
echo   video memory. A card with 10 GB is plenty.
echo.
echo   Use HD Textures?
echo.
echo     y  =  yes, make them now
echo     n  =  no  (this is the normal setting)
echo.
set "PICK="
set /p "PICK=  Type y or n and press Enter: "
if /i "%PICK%"=="y" goto build
if /i "%PICK%"=="n" goto off
goto menu

:build
cls
"%PY%" "%TOOL%" --set all
if errorlevel 1 (
  echo.
  echo   Something went wrong. Nothing in your game
  echo   folder was changed. Please send the text
  echo   above to the mod page.
)
echo.
pause
exit /b 0

:off
echo.
echo   HD Textures are off.
echo.
echo   Deleting them puts the game back to normal.
echo   Only the files this made are removed.
echo.
set "PICK="
set /p "PICK=  Delete the HD texture files? (y/n): "
if /i not "%PICK%"=="y" goto done
"%PY%" "%TOOL%" --remove
:done
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
