# -*- coding: utf-8 -*-
"""MP4 lezen en terugschrijven via FFmpeg.

Het beeld wordt gekopieerd, niet opnieuw gecomprimeerd. Alleen de audio
wordt vervangen door de vertaalde mix.
"""
import json
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


def _find_ffprobe(ffmpeg):
    name = "ffprobe.exe" if os.name == "nt" else "ffprobe"
    sibling = Path(ffmpeg).with_name(name)
    if sibling.is_file():
        return str(sibling)
    return shutil.which("ffprobe")


def _raw_popen(cmd):
    """ffmpeg zonder tekst op stdout, zodat de ruwe bytes zuiver blijven."""
    kwargs = {"stdout": subprocess.PIPE, "stderr": subprocess.DEVNULL, "stdin": subprocess.DEVNULL}
    if os.name == "nt":
        kwargs["creationflags"] = subprocess.CREATE_NO_WINDOW
    return subprocess.Popen(cmd, **kwargs)


def probe_media(path):
    """Duur in seconden en beeldsnelheid. (0.0, 25.0) als het beeldtempo ontbreekt."""
    ffmpeg = find_ffmpeg()
    if not ffmpeg or not path or not Path(path).is_file():
        return 0.0, 25.0
    probe = _find_ffprobe(ffmpeg)
    if not probe:
        return 0.0, 25.0
    cmd = [probe, "-v", "error", "-show_entries", "format=duration:stream=avg_frame_rate,codec_type", "-of", "json", str(path)]
    try:
        proc = _raw_popen(cmd)
        raw, _ = proc.communicate(timeout=20)
    except Exception:
        return 0.0, 25.0
    try:
        data = json.loads(raw.decode("utf-8", "replace") or "{}")
    except Exception:
        return 0.0, 25.0
    duration = 0.0
    try:
        duration = float((data.get("format") or {}).get("duration") or 0)
    except (TypeError, ValueError):
        duration = 0.0
    fps = 25.0
    for stream in data.get("streams") or []:
        if stream.get("codec_type") != "video":
            continue
        rate = str(stream.get("avg_frame_rate") or "")
        if "/" in rate:
            num, den = rate.split("/", 1)
            try:
                fps = float(num) / float(den) if float(den) else 25.0
            except (TypeError, ValueError, ZeroDivisionError):
                fps = 25.0
        break
    if fps < 1 or fps > 120:
        fps = 25.0
    return max(0.0, duration), fps


def read_video_frame(path, width, height, start=0.0):
    """Eén RGB-frame op het gevraagde formaat, of None."""
    ffmpeg = find_ffmpeg()
    if not ffmpeg or not path or not Path(path).is_file():
        return None
    width, height = max(2, int(width) // 2 * 2), max(2, int(height) // 2 * 2)
    vf = f"scale={width}:{height}:force_original_aspect_ratio=decrease,pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:color=black"
    cmd = [ffmpeg, "-hide_banner", "-loglevel", "error", "-ss", f"{max(0.0, float(start)):.3f}", "-i", str(path), "-frames:v", "1", "-an", "-vf", vf, "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    try:
        proc = _raw_popen(cmd)
        raw, _ = proc.communicate(timeout=30)
    except Exception:
        return None
    need = width * height * 3
    if not raw or len(raw) < need:
        return None
    return width, height, raw[:need]


def open_video_stream(path, width, height, start=0.0):
    """Doorlopende RGB-frames vanaf start. stdout levert width*height*3 bytes per frame."""
    ffmpeg = find_ffmpeg()
    if not ffmpeg:
        return None
    width, height = max(2, int(width) // 2 * 2), max(2, int(height) // 2 * 2)
    vf = f"scale={width}:{height}:force_original_aspect_ratio=decrease,pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:color=black"
    cmd = [ffmpeg, "-hide_banner", "-loglevel", "error", "-ss", f"{max(0.0, float(start)):.3f}", "-i", str(path), "-an", "-vf", vf, "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    return _raw_popen(cmd)


def open_audio_pcm(path, start=0.0, samplerate=48000, channels=2):
    """Ruwe float32-audio vanaf start, of None."""
    ffmpeg = find_ffmpeg()
    if not ffmpeg or not path or not Path(path).is_file():
        return None
    cmd = [ffmpeg, "-hide_banner", "-loglevel", "error", "-ss", f"{max(0.0, float(start)):.3f}", "-i", str(path), "-vn", "-f", "f32le", "-ac", str(int(channels)), "-ar", str(int(samplerate)), "-"]
    return _raw_popen(cmd)


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
