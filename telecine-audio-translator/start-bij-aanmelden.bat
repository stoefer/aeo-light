@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"
if not exist ".venv\Scripts\pythonw.exe" (
  echo Dubbelklik eerst op installeer.bat.
  pause
  exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0maak-snelkoppeling.ps1" -Doel Opstarten
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
