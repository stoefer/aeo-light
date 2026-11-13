#!/bin/sh
set -eu
cd "$(dirname "$0")"
if [ ! -x .venv/bin/python ]; then
  echo "Voer eerst ./installeer.sh uit." >&2
  exit 1
fi
mkdir -p "$HOME/.config/autostart"
APP_DIR="$(pwd)"
exec_field="$APP_DIR/start.sh"
case "$exec_field" in
  *[[:space:]\"\\\$\`]*)
    exec_field=$(printf '%s' "$exec_field" | sed 's/\\/\\\\/g; s/"/\\"/g; s/`/\\`/g; s/\$/\\$/g')
    exec_field="\"$exec_field\""
    ;;
esac
cat > "$HOME/.config/autostart/telecine-audio-translator.desktop" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=TeleCine Audio Translator
Comment=Vertaal filmgeluid naar het Vlaams
Exec=$exec_field
Path=$APP_DIR
Terminal=false
X-GNOME-Autostart-enabled=true
EOF
echo "Het programma start voortaan wanneer je aanmeldt."
echo "Zet het uit door dit bestand te verwijderen:"
echo "  $HOME/.config/autostart/telecine-audio-translator.desktop"
