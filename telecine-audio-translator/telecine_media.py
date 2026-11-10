# -*- coding: utf-8 -*-
"""MP4 lezen en terugschrijven via FFmpeg.

Het beeld wordt gekopieerd, niet opnieuw gecomprimeerd. Alleen de audio
wordt vervangen door de vertaalde mix.
"""
import os
import shutil
import subprocess
import threading
import time
from pathlib import Path


class MediaStopped(Exception):
    pass


def is_video_file(path):
    """True voor containers waarvan het beeld behouden kan blijven."""
    return Path(path).suffix.lower() in {".mp4", ".m4v"}


def find_ffmpeg():
    """Zoek ffmpeg op PATH, naast het programma, of op een bekende Windows-plek."""
    names = ("ffmpeg.exe", "ffmpeg") if os.name == "nt" else ("ffmpeg",)
    found = shutil.which("ffmpeg")
    candidates = []
    if os.environ.get("FFMPEG"):
        candidates.append(Path(os.environ["FFMPEG"]))
    if found:
        candidates.append(Path(found))
    here = Path(__file__).resolve().parent
    for name in names:
        candidates.append(here / name)
    if os.name == "nt":
        roots = []
        for key in ("ProgramFiles", "ProgramFiles(x86)", "LOCALAPPDATA"):
            if os.environ.get(key):
                roots.append(Path(os.environ[key]))
        for root in roots:
            candidates.append(root / "ffmpeg" / "bin" / "ffmpeg.exe")
            candidates.append(root / "Microsoft" / "WinGet" / "Links" / "ffmpeg.exe")
        candidates.append(Path(r"C:\ffmpeg\bin\ffmpeg.exe"))
        program_data = os.environ.get("ProgramData")
        if program_data:
            candidates.append(Path(program_data) / "chocolatey" / "bin" / "ffmpeg.exe")
    for candidate in candidates:
        if candidate and candidate.is_file():
            return str(candidate)
    return None


def ffmpeg_missing_message():
    return (
        "FFmpeg is nodig om een MP4 te lezen en de vertaling terug te schrijven.\n\n"
        "Installeer FFmpeg en zet het in PATH:\nhttps://ffmpeg.org/download.html\n\n"
        "Of plaats ffmpeg.exe in de programmamap."
    )


def _popen(cmd):
    kwargs = {
        "stdout": subprocess.PIPE,
        "stderr": subprocess.STDOUT,
        "stdin": subprocess.DEVNULL,
    }
    if os.name == "nt":
        kwargs["creationflags"] = subprocess.CREATE_NO_WINDOW
    return subprocess.Popen(cmd, **kwargs)


def run_tool(cmd, stop=None, child_setter=None):
    """Voer ffmpeg uit. Stop zet het proces stop en gooit MediaStopped."""
    proc = _popen(cmd)
    if child_setter:
        child_setter(proc)
    chunks = []
    lock = threading.Lock()

    def _read():
        try:
            while True:
                block = proc.stdout.read(4096)
                if not block:
                    break
                with lock:
                    chunks.append(block)
                    if sum(len(part) for part in chunks) > 200000:
                        del chunks[:-40]
        except Exception:
            pass

    reader = threading.Thread(target=_read, daemon=True)
    reader.start()
    try:
        while proc.poll() is None:
            if stop is not None and stop.is_set():
                _stop_process(proc)
                raise MediaStopped()
            time.sleep(0.2)
        reader.join(timeout=2)
        code = proc.returncode
    finally:
        if child_setter:
            child_setter(None)
    if stop is not None and stop.is_set():
        raise MediaStopped()
    if code != 0:
        with lock:
            tail = b"".join(chunks)[-2500:]
        text = tail.decode("utf-8", errors="replace").strip()
        raise RuntimeError(text or "ffmpeg stopte met een fout")
    with lock:
        output = b"".join(chunks)
    return output.decode("utf-8", errors="replace")


def _stop_process(proc):
    if proc.poll() is not None:
        return
    try:
        proc.terminate()
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            proc.kill()
        except Exception:
            pass
    except Exception:
        pass


def require_ffmpeg():
    ffmpeg = find_ffmpeg()
    if not ffmpeg:
        raise RuntimeError(ffmpeg_missing_message())
    return ffmpeg


def extract_wav(video, wav_out, stop=None, child_setter=None):
    """Haal het eerste audiospoor uit een video en schrijf PCM-WAV."""
    ffmpeg = require_ffmpeg()
    target = Path(wav_out)
    target.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        ffmpeg, "-hide_banner", "-y",
        "-i", str(video),
        "-vn", "-map", "0:a:0",
        "-c:a", "pcm_s16le",
        str(target),
    ]
    try:
        run_tool(cmd, stop, child_setter)
    except MediaStopped:
        target.unlink(missing_ok=True)
        raise
    except RuntimeError as exc:
        target.unlink(missing_ok=True)
        low = str(exc).lower()
        if "matches no streams" in low or "does not contain any stream" in low or "output file #0 does not contain" in low:
            raise RuntimeError("Deze MP4 heeft geen audiospoor.") from exc
        raise RuntimeError("Audio uit de MP4 halen is mislukt.\n" + str(exc)) from exc
    if not target.is_file() or target.stat().st_size < 44:
        target.unlink(missing_ok=True)
        raise RuntimeError("Deze MP4 heeft geen bruikbare audio.")
    return target


def write_mp4(video, wav, mp4_out, stop=None, child_setter=None, subtitles=None):
    """Schrijf een MP4 met het oorspronkelijke beeld en de nieuwe audio.

    Het beeld wordt gekopieerd. De audio wordt AAC, zodat gewone spelers
    het bestand openen. Het beeld wordt niet ingekort als de audio korter is.
    Een SRT wordt als zacht ondertitelspoor meegezet. Lukt dat niet, dan
    komt de film er zonder dat spoor toch.
    """
    ffmpeg = require_ffmpeg()
    target = Path(mp4_out)
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_name(target.stem + ".part.mp4")
    attempts = [subtitles] if subtitles else [None]
    if subtitles:
        attempts.append(None)
    last_error = None
    for srt in attempts:
        partial.unlink(missing_ok=True)
        cmd = [
            ffmpeg, "-hide_banner", "-y",
            "-i", str(video),
            "-i", str(wav),
        ]
        if srt:
            cmd += ["-i", str(srt)]
        cmd += ["-map", "0:v:0", "-map", "1:a:0"]
        if srt:
            cmd += ["-map", "2:0", "-c:s", "mov_text", "-metadata:s:s:0", "language=nld"]
        cmd += ["-c:v", "copy", "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", str(partial)]
        try:
            run_tool(cmd, stop, child_setter)
        except MediaStopped:
            partial.unlink(missing_ok=True)
            raise
        except RuntimeError as exc:
            partial.unlink(missing_ok=True)
            last_error = exc
            if srt:
                continue
            low = str(exc).lower()
            if "matches no streams" in low or "does not contain any stream" in low:
                raise RuntimeError("Deze MP4 heeft geen videobeeld om te behouden.") from exc
            raise RuntimeError("De vertaalde MP4 kon niet geschreven worden.\n" + str(exc)) from exc
        if not partial.is_file() or partial.stat().st_size < 32:
            partial.unlink(missing_ok=True)
            last_error = RuntimeError("De vertaalde MP4 is leeg.")
            if srt:
                continue
            raise last_error
        partial.replace(target)
        return target
    raise RuntimeError("De vertaalde MP4 kon niet geschreven worden.\n" + str(last_error or ""))
