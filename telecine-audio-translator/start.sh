#!/bin/sh
cd "$(dirname "$0")" || exit 1
if [ ! -x .venv/bin/python ] || [ ! -f .venv/.telecine-ready ]; then
  echo "Eerste start: TeleCine Audio Translator wordt geinstalleerd."
  echo "Dit kan enkele minuten duren en heeft internet nodig."
  ./installeer.sh || exit 1
fi
exec .venv/bin/python TeleCine_AudioTranslator.py
