# -*- coding: utf-8 -*-
"""Verhoog het laatste deel van VERSION, bijvoorbeeld 0.9.01 naar 0.9.02.

GitHub Actions doet dit na elke release-tag. Het kan ook met de hand,
zodra de huidige versie al als release bestaat:

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


def should_bump_after_release(current, released):
    """Alleen verhogen als de branch nog op het zojuist gereleasde nummer staat."""
    return current.strip() == released.strip()


def bump_files(root):
    """Schrijf het volgende nummer in VERSION en in de eerste versieregel van README."""
    root = Path(root)
    path = root / "VERSION"
    current = path.read_text(encoding="utf-8").strip()
    new = bump(current)
    readme = root / "README.md"
    if readme.exists():
        text = readme.read_text(encoding="utf-8")
        old = "Versie **{0}**.".format(current)
        if old not in text:
            raise ValueError("README noemt {0} niet, dus het nummer is niet mee verhoogd.".format(current))
        readme.write_text(text.replace(old, "Versie **{0}**.".format(new), 1), encoding="utf-8")
    path.write_text(new + "\n", encoding="utf-8")
    return new


def main():
    print(bump_files(Path(__file__).resolve().parent))


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(exc, file=sys.stderr)
        raise SystemExit(1)
