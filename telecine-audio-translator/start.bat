@echo off
setlocal EnableExtensions
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
cd /d "%ROOT%"
chcp 65001 >nul
if not exist "%ROOT%\.venv\Scripts\python.exe" (
  echo De omgeving is nog niet geinstalleerd.
  echo Dubbelklik eerst op installeer.bat.
  pause
  exit /b 1
)
"%ROOT%\.venv\Scripts\python.exe" "%ROOT%\TeleCine_AudioTranslator.py"
if errorlevel 1 pause
