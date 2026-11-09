# -*- coding: utf-8 -*-
"""Eenmalige voorbereiding: Vlaamse Piper-stemmen en Argos-vertaalpakketten."""
import shutil
import sys
import urllib.request
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent
VOICE_DIR = BASE_DIR / "voices"
VOICE_BASE = "https://huggingface.co/rhasspy/piper-voices/resolve/main"
VOICE_FILES = [
    "nl/nl_BE/nathalie/medium/nl_BE-nathalie-medium.onnx",
    "nl/nl_BE/nathalie/medium/nl_BE-nathalie-medium.onnx.json",
    "nl/nl_BE/rdh/medium/nl_BE-rdh-medium.onnx",
    "nl/nl_BE/rdh/medium/nl_BE-rdh-medium.onnx.json",
]
ARGOS_PAIRS = (("fr", "nl"), ("en", "nl"))


def download_voices():
    """Download ontbrekende Piper-stemmen naar voices/."""
    VOICE_DIR.mkdir(parents=True, exist_ok=True)
    ok = True
    for rel in VOICE_FILES:
        name = rel.rsplit("/", 1)[-1]
        target = VOICE_DIR / name
        if _voice_file_ok(target):
            print(f"Al aanwezig: {name}")
            continue
        url = f"{VOICE_BASE}/{rel}"
        print(f"Downloaden: {name}")
        partial = target.with_suffix(target.suffix + ".part")
        try:
            request = urllib.request.Request(url, headers={"User-Agent": "TeleCine-Audio-Translator"})
            with urllib.request.urlopen(request, timeout=120) as response, partial.open("wb") as handle:
                shutil.copyfileobj(response, handle)
            if not _voice_file_ok(partial, target.name):
                raise RuntimeError("download is onvolledig of geen geldig stem-bestand")
            partial.replace(target)
        except Exception as exc:
            ok = False
            print(f"Mislukt: {name}: {exc}", file=sys.stderr)
            partial.unlink(missing_ok=True)
            if target.exists() and not _voice_file_ok(target):
                target.unlink()
    return 0 if ok else 1


def _voice_file_ok(path, final_name=None):
    """Weiger lege bestanden, HTML-foutpagina's en Git-LFS-verwijzingen."""
    if not path.exists():
        return False
    name = (final_name or path.name).lower()
    size = path.stat().st_size
    if name.endswith(".json"):
        if size < 50:
            return False
        head = path.read_text(encoding="utf-8", errors="replace").lstrip()
        return head.startswith("{")
    if name.endswith(".onnx"):
        if size < 1_000_000:
            return False
        head = path.read_bytes()[:80]
        return not head.startswith(b"version https://git-lfs") and not head.lstrip().startswith(b"<")
    return False


def _argos_ready(source):
    import argostranslate.translate as tr
    installed = tr.get_installed_languages()
    src = next((lang for lang in installed if lang.code == source), None)
    dst = next((lang for lang in installed if lang.code == "nl"), None)
    return bool(src and dst and src.get_translation(dst))


def ensure_argos_package(source):
    """Installeer het Argos-pakket source -> nl als het nog ontbreekt."""
    import argostranslate.package as pkg
    if _argos_ready(source):
        return
    pkg.update_package_index()
    match = next((item for item in pkg.get_available_packages()
                  if item.from_code == source and item.to_code == "nl"), None)
    if match is None:
        raise RuntimeError(
            f"Geen Argos-vertaalpakket gevonden van '{source}' naar Nederlands.\n"
            "Controleer de internetverbinding en voer installeer.bat opnieuw uit."
        )
    pkg.install_from_path(match.download())


def install_argos():
    """Installeer Frans->Nederlands en Engels->Nederlands."""
    try:
        import argostranslate.package  # noqa: F401
    except ImportError:
        print("argostranslate ontbreekt. Installeer eerst de Python-onderdelen.", file=sys.stderr)
        return 1
    try:
        for source, _dest in ARGOS_PAIRS:
            if _argos_ready(source):
                print(f"Al geinstalleerd: {source} -> nl")
                continue
            print(f"Installeren: {source} -> nl")
            ensure_argos_package(source)
    except Exception as exc:
        print(f"Vertaalpakketten installeren mislukt: {exc}", file=sys.stderr)
        return 1
    return 0


def main(argv):
    command = argv[1] if len(argv) > 1 else "voices"
    if command == "voices":
        return download_voices()
    if command == "argos":
        return install_argos()
    print("Gebruik: python telecine_setup.py [voices|argos]", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
