@echo off
setlocal EnableExtensions
rem Pad vastleggen voor chcp. Een slash vlak voor een aanhalingsteken breekt cmd.
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
cd /d "%ROOT%"
if errorlevel 1 (
  echo De programmamap is niet bereikbaar:
  echo %ROOT%
  pause
  exit /b 1
)
chcp 65001 >nul
if not exist "%ROOT%\requirements.txt" (
  echo requirements.txt staat niet naast installeer.bat.
  echo Gevonden map:
  echo %ROOT%
  echo.
  echo Pak de zip helemaal uit naar een gewone map, bijvoorbeeld je Bureaublad.
  echo Dubbelklik installeer.bat niet vanuit het zip-bestand of vanuit Directory Opus.
  echo In die weergave komt het bestand in een tijdelijke dtemp-map terecht.
  pause
  exit /b 1
)

set "APPVER=onbekend"
if not exist "%ROOT%\VERSION" goto version_done
for /f "usebackq delims=" %%V in ("%ROOT%\VERSION") do set "APPVER=%%V"
:version_done
echo.
echo TeleCine Audio Translator installeren
echo =====================================
echo Versie %APPVER%
echo Dit maakt een programma dat je daarna met een dubbelklik start.
echo Er verschijnt dan geen zwart opdrachtvenster.
echo.

rem Stemscheiding heeft een kant-en-klaar pakket voor Python 3.12.
rem Python 3.14 probeert diffq-fixed te compileren en mist dan bitpack.pyx.
set "PY="
set "HAVE312=0"
where py >nul 2>&1
if %errorlevel%==0 (
  py -3.12 -c "import sys" >nul 2>&1
  if not errorlevel 1 set "HAVE312=1"
)
if "%HAVE312%"=="1" (
  set "PY=py -3.12"
  echo Python 3.12 gevonden. De omgeving gebruikt die versie.
) else (
  echo Python 3.12 is niet gevonden.
  echo Stemscheiding ^(UVR^) heeft Python 3.12 nodig. Python 3.14 heeft geen kant-en-klaar pakket.
  echo Installeer Python 3.12.10, 64-bit, en vink "Add python.exe to PATH" en tcl/tk aan:
  echo https://www.python.org/ftp/python/3.12.10/python-3.12.10-amd64.exe
  echo Voer installeer.bat daarna opnieuw uit. Tot dan: vink in het venster stemscheiding uit.
  echo.
  where py >nul 2>&1
  if %errorlevel%==0 (
    set "PY=py -3"
  ) else (
    where python >nul 2>&1
    if errorlevel 1 (
      echo Python 3 is niet gevonden.
      echo Installeer Python 3.12 van de link hierboven.
      pause
      exit /b 1
    )
    set "PY=python"
  )
)

rem De python-regel staat buiten haakjes: een sluitend haakje zou anders het if-blok sluiten.
if not "%HAVE312%"=="1" goto skip_rebuild
if not exist "%ROOT%\.venv\Scripts\python.exe" goto skip_rebuild
"%ROOT%\.venv\Scripts\python.exe" -c "import sys; raise SystemExit(0 if sys.version_info[0]==3 and sys.version_info[1]==12 else 1)"
if errorlevel 1 (
  echo De bestaande omgeving is geen Python 3.12 en wordt opnieuw gemaakt.
  rmdir /s /q "%ROOT%\.venv"
  if exist "%ROOT%\.venv\Scripts\python.exe" (
    echo De oude omgeving is nog in gebruik. Sluit TeleCine Audio Translator en voer installeer.bat opnieuw uit.
    pause
    exit /b 1
  )
)
:skip_rebuild

if not exist "%ROOT%\.venv\Scripts\python.exe" (
  echo Virtuele omgeving aanmaken...
  %PY% -m venv "%ROOT%\.venv"
  if errorlevel 1 (
    echo De virtuele omgeving kon niet aangemaakt worden.
    pause
    exit /b 1
  )
)

"%ROOT%\.venv\Scripts\python.exe" -c "import tkinter" >nul 2>&1
if errorlevel 1 (
  echo Tkinter ontbreekt, daardoor kan het venster niet openen.
  echo Herinstalleer Python 3.12 en vink tcl/tk aan.
  pause
  exit /b 1
)

echo Python-onderdelen installeren. Dit kan enkele minuten duren...
"%ROOT%\.venv\Scripts\python.exe" -m pip install --upgrade pip
"%ROOT%\.venv\Scripts\python.exe" -m pip install -r "%ROOT%\requirements.txt"
if errorlevel 1 (
  echo Installatie van de Python-onderdelen is mislukt.
  pause
  exit /b 1
)

echo.
echo Stemscheiding ^(UVR^) installeren. Dit is een grote download...
"%ROOT%\.venv\Scripts\python.exe" -m pip install --only-binary=diffq-fixed -r "%ROOT%\requirements-uvr.txt"
if errorlevel 1 (
  echo UVR is niet geinstalleerd. Het programma start wel.
  echo Op Python 3.14 ontbreekt het kant-en-klare pakket. Gebruik Python 3.12 en voer installeer.bat opnieuw uit.
  echo Tot dan: vink in het venster "Stem met UVR scheiden" uit.
)

echo.
echo Vlaamse stemmen downloaden...
"%ROOT%\.venv\Scripts\python.exe" "%ROOT%\telecine_setup.py" voices
if errorlevel 1 (
  echo De stemmen zijn niet volledig gedownload. Controleer de internetverbinding en voer installeer.bat opnieuw uit.
)

echo.
echo Vertaalpakketten Frans en Engels naar Nederlands...
"%ROOT%\.venv\Scripts\python.exe" "%ROOT%\telecine_setup.py" argos
if errorlevel 1 (
  echo De vertaalpakketten zijn nog niet geinstalleerd. Het programma probeert ze bij de eerste vertaling opnieuw.
)

echo.
echo Snelkoppeling op het bureaublad plaatsen...
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\maak-snelkoppeling.ps1" -Doel Bureaublad
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
echo Voor MP4 als bron en resultaat is FFmpeg nodig.
where ffmpeg >nul 2>&1
if errorlevel 1 (
  echo FFmpeg is niet gevonden. WAV werkt wel.
  echo Installeer FFmpeg en zet het in PATH: https://ffmpeg.org/download.html
  echo Of plaats ffmpeg.exe in deze map.
) else (
  echo FFmpeg is gevonden. Een MP4 kan als bron en als resultaat.
)
echo.
pause
