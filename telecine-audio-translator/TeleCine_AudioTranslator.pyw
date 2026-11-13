# -*- coding: utf-8 -*-
"""Dubbelklik-starter voor Windows. Opent het venster zonder zwart opdrachtvenster."""
import os
import sys
from pathlib import Path

BASE = Path(__file__).resolve().parent
SCRIPT = BASE / "TeleCine_AudioTranslator.py"

def venv_python():
    if os.name == "nt":
        return BASE / ".venv" / "Scripts" / "pythonw.exe"
    return BASE / ".venv" / "bin" / "python"

py = venv_python()
if py.exists():
    try:
        same = Path(sys.executable).resolve() == py.resolve()
    except OSError:
        same = False
    if not same:
        os.execv(str(py), [str(py), str(SCRIPT)])

os.chdir(BASE)
import runpy
runpy.run_path(str(SCRIPT), run_name="__main__")
