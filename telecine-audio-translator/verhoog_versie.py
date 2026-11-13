# -*- coding: utf-8 -*-
"""Verhoog het laatste deel van VERSION, bijvoorbeeld 0.9.01 naar 0.9.02.

De volgende release leest dit bestand, verhoogt het, en publiceert tag v<versie>.
Draai dit pas als de huidige versie al als release bestaat:

  python verhoog_versie.py
"""
import sys
from pathlib import Path


def bump(text):
    """0.9.01 wordt 0.9.02. De breedte van het laatste deel blijft gelijk."""
    parts = text.strip().split(".")
    if len(parts) < 2 or not parts[-1].isdigit() or any(not part.isdigit() for part in parts):
        raise ValueError("Onbekende versie: {0}".format(text.strip()))
    width = len(parts[-1])
    parts[-1] = str(int(parts[-1]) + 1).zfill(width)
    return ".".join(parts)


def main():
    root = Path(__file__).resolve().parent
    path = root / "VERSION"
    current = path.read_text(encoding="utf-8").strip()
    new = bump(current)
    readme = root / "README.md"
    readme_text = None
    if readme.exists():
        old = "Versie **{0}**.".format(current)
        readme_text = readme.read_text(encoding="utf-8")
        if old not in readme_text:
            raise ValueError("README noemt {0} niet, dus het nummer is niet mee verhoogd.".format(current))
        readme_text = readme_text.replace(old, "Versie **{0}**.".format(new), 1)
    path.write_text(new + "\n", encoding="utf-8")
    if readme_text is not None:
        readme.write_text(readme_text, encoding="utf-8")
    print(new)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(exc, file=sys.stderr)
        raise SystemExit(1)
