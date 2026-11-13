#!/bin/sh
set -eu
cd "$(dirname "$0")"
PY="${PYTHON:-python3}"

echo "TeleCine Audio Translator installeren"
if ! "$PY" -c "import tkinter" >/dev/null 2>&1; then
  echo "Tkinter ontbreekt, daardoor kan het venster niet openen."
  echo "Op Ubuntu/Debian: sudo apt install python3-tk python3-venv"
  exit 1
fi
if [ -x .venv/bin/python ] && ! .venv/bin/python -m pip --version >/dev/null 2>&1; then
  echo "De virtuele omgeving is onvolledig en wordt opnieuw aangemaakt."
  rm -rf .venv
fi
if [ ! -x .venv/bin/python ]; then
  if ! "$PY" -m venv .venv; then
    echo "De virtuele omgeving kon niet aangemaakt worden."
    echo "Op Ubuntu/Debian: sudo apt install python3-venv python3-tk"
    exit 1
  fi
fi
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install -r requirements.txt
touch .venv/.telecine-ready
if ! .venv/bin/python -m pip install --only-binary=diffq-fixed -r requirements-uvr.txt; then
  echo "UVR is niet geinstalleerd. Het programma start wel; vink stemscheiding uit."
  echo "Het bronpakket diffq-fixed mist bitpack.pyx. Alleen een kant-en-klaar pakket wordt gebruikt."
fi
.venv/bin/python telecine_setup.py voices || echo "Stemmen zijn niet volledig gedownload."
.venv/bin/python telecine_setup.py argos || echo "Vertaalpakketten volgen bij de eerste vertaling."
chmod +x start.sh start-bij-aanmelden.sh
if [ ! -f /usr/share/espeak-ng-data/phontab ] && [ ! -f /usr/lib/x86_64-linux-gnu/espeak-ng-data/phontab ] && [ ! -f /usr/lib/aarch64-linux-gnu/espeak-ng-data/phontab ]; then
  echo "Tip: installeer eSpeak NG voor de Vlaamse uitspraak."
  echo "  sudo apt install espeak-ng"
fi

APP_DIR="$(pwd)"
exec_field="$APP_DIR/start.sh"
case "$exec_field" in
  *[[:space:]\"\\\$\`]*)
    exec_field=$(printf '%s' "$exec_field" | sed 's/\\/\\\\/g; s/"/\\"/g; s/`/\\`/g; s/\$/\\$/g')
    exec_field="\"$exec_field\""
    ;;
esac
mkdir -p "$HOME/.local/share/applications"
cat > "$HOME/.local/share/applications/telecine-audio-translator.desktop" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=TeleCine Audio Translator
Comment=Vertaal filmgeluid naar het Vlaams
Exec=$exec_field
Path=$APP_DIR
Icon=audio-x-generic
Terminal=false
Categories=AudioVideo;Audio;
StartupNotify=true
EOF

echo
echo "Klaar. Start het programma met:"
echo "  $APP_DIR/start.sh"
echo "of zoek in het programmamenu naar TeleCine Audio Translator."
echo "Bij aanmelden starten: ./start-bij-aanmelden.sh"
if command -v ffmpeg >/dev/null 2>&1; then
  echo "FFmpeg is gevonden. Een MP4 kan als bron en als resultaat."
else
  echo "FFmpeg is niet gevonden. WAV werkt wel."
  echo "Voor MP4: sudo apt install ffmpeg"
fi
