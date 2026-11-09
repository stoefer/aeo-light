@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"
if not exist ".venv\Scripts\python.exe" (
  echo De omgeving is nog niet geinstalleerd.
  echo Dubbelklik eerst op installeer.bat.
  pause
  exit /b 1
)
".venv\Scripts\python.exe" "%~dp0TeleCine_AudioTranslator.py"
if errorlevel 1 pause
