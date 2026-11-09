@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"

echo.
echo TeleCine Audio Translator installeren
echo =====================================
echo Dit maakt een programma dat je daarna met een dubbelklik start.
echo Er verschijnt dan geen zwart opdrachtvenster.
echo.

where py >nul 2>&1
if %errorlevel%==0 (
  set "PY=py -3"
) else (
  where python >nul 2>&1
  if errorlevel 1 (
    echo Python 3 is niet gevonden.
    echo Installeer Python 3 van https://www.python.org/downloads/
    echo en vink "Add python.exe to PATH" aan.
    pause
    exit /b 1
  )
  set "PY=python"
)

if not exist ".venv\Scripts\python.exe" (
  echo Virtuele omgeving aanmaken...
  %PY% -m venv .venv
  if errorlevel 1 (
    echo De virtuele omgeving kon niet aangemaakt worden.
    pause
    exit /b 1
  )
)

".venv\Scripts\python.exe" -c "import tkinter" >nul 2>&1
if errorlevel 1 (
  echo Tkinter ontbreekt, daardoor kan het venster niet openen.
  echo Herinstalleer Python en vink tcl/tk aan.
  pause
  exit /b 1
)

echo Python-onderdelen installeren. Dit kan enkele minuten duren...
".venv\Scripts\python.exe" -m pip install --upgrade pip
".venv\Scripts\python.exe" -m pip install -r "%~dp0requirements.txt"
if errorlevel 1 (
  echo Installatie van de Python-onderdelen is mislukt.
  pause
  exit /b 1
)

echo.
echo Stemscheiding ^(UVR^) installeren. Dit is een grote download...
".venv\Scripts\python.exe" -m pip install -r "%~dp0requirements-uvr.txt"
if errorlevel 1 (
  echo UVR is niet geinstalleerd. Het programma start wel.
  echo Vink in het venster "Stem met UVR scheiden" uit, of voer installeer.bat later opnieuw uit.
)

echo.
echo Vlaamse stemmen downloaden...
".venv\Scripts\python.exe" "%~dp0telecine_setup.py" voices
if errorlevel 1 (
  echo De stemmen zijn niet volledig gedownload. Controleer de internetverbinding en voer installeer.bat opnieuw uit.
)

echo.
echo Vertaalpakketten Frans en Engels naar Nederlands...
".venv\Scripts\python.exe" "%~dp0telecine_setup.py" argos
if errorlevel 1 (
  echo De vertaalpakketten zijn nog niet geinstalleerd. Het programma probeert ze bij de eerste vertaling opnieuw.
)

echo.
echo Snelkoppeling op het bureaublad plaatsen...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0maak-snelkoppeling.ps1" -Doel Bureaublad
if errorlevel 1 (
  echo De snelkoppeling kon niet aangemaakt worden.
  echo Je kunt start.vbs in deze map nog wel dubbelklikken.
)

echo.
echo Klaar.
echo Start het programma via het bureaublad-icoon "TeleCine Audio Translator".
echo.
echo Optioneel: voer start-bij-aanmelden.bat uit als het venster ook moet
echo openen telkens je bij Windows aanmeldt.
echo.
echo Voor Vlaamse uitspraak is eSpeak NG nodig:
echo https://github.com/espeak-ng/espeak-ng/releases
echo.
pause
