#!/bin/sh
cd "$(dirname "$0")" || exit 1
if [ ! -x .venv/bin/python ] || [ ! -f .venv/.telecine-ready ]; then
  echo "Eerste start: TeleCine Audio Translator wordt geinstalleerd."
  echo "Dit kan enkele minuten duren en heeft internet nodig."
  ./installeer.sh || exit 1
fi
if [ ! -f voices/nl_BE-nathalie-medium.onnx ] || [ ! -f voices/nl_BE-rdh-medium.onnx ]; then
  echo "Vlaamse stemmen ontbreken nog. Opnieuw downloaden..."
  .venv/bin/python telecine_setup.py voices || echo "Stemmen zijn niet volledig gedownload. Het venster zegt welke stem ontbreekt."
fi
exec .venv/bin/python TeleCine_AudioTranslator.py
