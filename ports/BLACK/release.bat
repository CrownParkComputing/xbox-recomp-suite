@echo off
cd /d "%~dp0"
set "OUT=out\build\release"
if not exist "%OUT%\BLACK.exe" call build.bat || exit /b 1
rem One name for every release, so releases/latest/download/BLACK-XBERecomp.zip stays a working link
set "ZIP=BLACK-XBERecomp.zip"
del /q "%ZIP%" 2>nul
rem an empty assets folder beside the executable, where the game (files or .iso) goes
rd /s /q "%OUT%\release" 2>nul
mkdir "%OUT%\release\assets"
7z a "%ZIP%" ".\%OUT%\BLACK.exe" ".\%OUT%\release\assets"
