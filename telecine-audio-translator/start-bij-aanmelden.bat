@echo off
setlocal EnableExtensions
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
cd /d "%ROOT%"
chcp 65001 >nul
if not exist "%ROOT%\.venv\Scripts\pythonw.exe" (
  echo Dubbelklik eerst op installeer.bat.
  pause
  exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\maak-snelkoppeling.ps1" -Doel Opstarten
if errorlevel 1 (
  echo De opstartkoppeling kon niet geplaatst worden.
  pause
  exit /b 1
)
echo.
echo Het programma start voortaan wanneer je bij Windows aanmeldt.
echo Verwijder de snelkoppeling uit de map Opstarten om dit uit te zetten.
echo.
pause
