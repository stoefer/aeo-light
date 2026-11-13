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


def parse_frame_rate(text):
    """Zet 24000/1001 of 25 om naar beelden per seconde."""
    raw = str(text or "").strip()
    if not raw or raw in {"0/0", "N/A"}:
        return 0.0
    if "/" in raw:
        num, den = raw.split("/", 1)
        try:
            den_f = float(den)
        except ValueError:
            return 0.0
        if den_f == 0:
            return 0.0
        return float(num) / den_f
    try:
        return float(raw)
    except ValueError:
        return 0.0


def timeline_from_probe(payload):
    """Duur, beelden per seconde en aantal frames uit de JSON van ffprobe."""
    streams = payload.get("streams") or []
    stream = streams[0] if streams else {}
    info = payload.get("format") or {}
    rate = parse_frame_rate(stream.get("avg_frame_rate")) or parse_frame_rate(stream.get("r_frame_rate"))
    fps = rate if rate > 1 else 25.0
    duration = _positive_float(stream.get("duration")) or _positive_float(info.get("duration"))
    frames = _positive_int(stream.get("nb_frames"))
    if frames <= 0 and duration > 0:
        frames = max(1, int(round(duration * fps)))
    if frames <= 0:
        frames = 1
    if duration <= 0:
        duration = frames / fps
    return {"duration": duration, "fps": fps, "frames": frames, "has_picture": True}


def timeline_for_audio(duration):
    """Tijdlijn zonder beeld: tien stappen per seconde."""
    seconds = max(0.0, float(duration or 0))
    fps = 10.0
    frames = max(1, int(round(seconds * fps)) or 1)
    return {"duration": max(seconds, (frames - 1) / fps), "fps": fps, "frames": frames, "has_picture": False}


def _positive_float(value):
    try:
        number = float(value)
    except (TypeError, ValueError):
        return 0.0
    return number if number > 0 else 0.0


def _positive_int(value):
    try:
        number = int(value)
    except (TypeError, ValueError):
        return 0
    return number if number > 0 else 0


def find_ffprobe():
    """ffprobe staat gewoonlijk naast ffmpeg."""
    ffmpeg = find_ffmpeg()
    if ffmpeg:
        path = Path(ffmpeg)
        sibling = path.with_name("ffprobe.exe" if path.suffix.lower() == ".exe" else "ffprobe")
        if sibling.is_file():
            return str(sibling)
    return shutil.which("ffprobe")


def probe_video(path):
    """Lees duur en aantal frames van het eerste videospoor."""
    ffprobe = find_ffprobe()
    if not ffprobe:
        raise RuntimeError("FFprobe hoort bij FFmpeg en is niet gevonden. Daarmee wordt het aantal frames bepaald.")
    cmd = [
        ffprobe, "-v", "error",
        "-select_streams", "v:0",
        "-show_entries", "stream=avg_frame_rate,r_frame_rate,duration,nb_frames:format=duration",
        "-of", "json",
        str(path),
    ]
    try:
        completed = subprocess.run(cmd, check=False, capture_output=True, text=True)
    except OSError as exc:
        raise RuntimeError("De film kon niet worden gelezen.\n" + str(exc)) from exc
    if completed.returncode != 0:
        raise RuntimeError("De film kon niet worden gelezen.\n" + (completed.stderr or completed.stdout or "").strip())
    try:
        payload = json.loads(completed.stdout or "{}")
    except json.JSONDecodeError as exc:
        raise RuntimeError("De filmgegevens zijn onleesbaar.") from exc
    if not payload.get("streams"):
        raise RuntimeError("Deze film heeft geen beeld.")
    return timeline_from_probe(payload)


def extract_frame_command(ffmpeg, video, seconds, png, accurate=False):
    """Eén frame. accurate zoekt het echte beeld, anders de dichtstbijzijnde snelle sprong."""
    stamp = f"{max(0.0, float(seconds)):.3f}"
    if accurate:
        return [ffmpeg, "-hide_banner", "-y", "-i", str(video), "-ss", stamp, "-frames:v", "1", str(png)]
    return [ffmpeg, "-hide_banner", "-y", "-ss", stamp, "-i", str(video), "-frames:v", "1", str(png)]


def extract_frame(video, seconds, png_out, accurate=False, stop=None):
    """Schrijf één frame als PNG."""
    ffmpeg = require_ffmpeg()
    target = Path(png_out)
    target.parent.mkdir(parents=True, exist_ok=True)
    run_tool(extract_frame_command(ffmpeg, video, seconds, target, accurate), stop)
    if not target.is_file() or target.stat().st_size < 16:
        raise RuntimeError("Dit frame kon niet uit de film gelezen worden.")
    return target


def extract_wav_command(ffmpeg, video, target, start=None, end=None):
    """Audio naar PCM. start en end zijn seconden op de bron."""
    cmd = [ffmpeg, "-hide_banner", "-y"]
    begin = None
    if start is not None and float(start) > 0:
        begin = float(start)
        cmd += ["-ss", f"{begin:.3f}"]
    cmd += ["-i", str(video)]
    if end is not None:
        origin = 0.0 if begin is None else begin
        cmd += ["-t", f"{max(0.01, float(end) - origin):.3f}"]
    cmd += ["-vn", "-map", "0:a:0", "-c:a", "pcm_s16le", str(target)]
    return cmd


def extract_wav(video, wav_out, stop=None, child_setter=None, start=None, end=None):
    """Haal het eerste audiospoor uit een video en schrijf PCM-WAV."""
    ffmpeg = require_ffmpeg()
    target = Path(wav_out)
    target.parent.mkdir(parents=True, exist_ok=True)
    cmd = extract_wav_command(ffmpeg, video, target, start, end)
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


def write_mp4(video, wav, mp4_out, stop=None, child_setter=None):
    """Schrijf een MP4 met het oorspronkelijke beeld en de nieuwe audio.

    Het beeld wordt gekopieerd. De audio wordt AAC, zodat gewone spelers
    het bestand openen. Het beeld wordt niet ingekort als de audio korter is.
    """
    ffmpeg = require_ffmpeg()
    target = Path(mp4_out)
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_name(target.stem + ".part.mp4")
    partial.unlink(missing_ok=True)
    cmd = [
        ffmpeg, "-hide_banner", "-y",
        "-i", str(video),
        "-i", str(wav),
        "-map", "0:v:0",
        "-map", "1:a:0",
        "-c:v", "copy",
        "-c:a", "aac",
        "-b:a", "192k",
        "-movflags", "+faststart",
        str(partial),
    ]
    try:
        run_tool(cmd, stop, child_setter)
    except MediaStopped:
        partial.unlink(missing_ok=True)
        raise
    except RuntimeError as exc:
        partial.unlink(missing_ok=True)
        low = str(exc).lower()
        if "matches no streams" in low or "does not contain any stream" in low:
            raise RuntimeError("Deze MP4 heeft geen videobeeld om te behouden.") from exc
        raise RuntimeError("De vertaalde MP4 kon niet geschreven worden.\n" + str(exc)) from exc
    if not partial.is_file() or partial.stat().st_size < 32:
        partial.unlink(missing_ok=True)
        raise RuntimeError("De vertaalde MP4 is leeg.")
    partial.replace(target)
    return target


def subtitle_mux_command(ffmpeg, video, srt, partial, with_audio=True):
    """FFmpeg-opdracht: beeld kopiëren, geluid laten staan, Nederlandse ondertitel toevoegen."""
    cmd = [
        ffmpeg, "-hide_banner", "-y",
        "-i", str(video),
        "-i", str(srt),
        "-map", "0:v:0",
    ]
    if with_audio:
        cmd += ["-map", "0:a:0"]
    cmd += ["-map", "1:0", "-c:v", "copy"]
    if with_audio:
        cmd += ["-c:a", "copy"]
    cmd += [
        "-c:s", "mov_text",
        "-metadata:s:s:0", "language=nld",
        "-disposition:s:0", "default",
        "-movflags", "+faststart",
        str(partial),
    ]
    return cmd


def write_subtitled_mp4(video, srt, mp4_out, stop=None, child_setter=None):
    """Schrijf een MP4 met het oorspronkelijke beeld, het oorspronkelijke geluid en de ondertitel.

    Het beeld wordt niet opnieuw gecomprimeerd. De Nederlandse ondertitel staat
    als standaardspoor in de film.
    """
    ffmpeg = require_ffmpeg()
    target = Path(mp4_out)
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_name(target.stem + ".part.mp4")
    partial.unlink(missing_ok=True)

    def _run(with_audio):
        partial.unlink(missing_ok=True)
        run_tool(subtitle_mux_command(ffmpeg, video, srt, partial, with_audio), stop, child_setter)

    try:
        try:
            _run(True)
        except RuntimeError as exc:
            low = str(exc).lower()
            if "matches no streams" in low or "does not contain any stream" in low:
                _run(False)
            else:
                raise
    except MediaStopped:
        partial.unlink(missing_ok=True)
        raise
    except RuntimeError as exc:
        partial.unlink(missing_ok=True)
        low = str(exc).lower()
        if "matches no streams" in low or "does not contain any stream" in low:
            raise RuntimeError("Deze MP4 heeft geen videobeeld om te behouden.") from exc
        raise RuntimeError("De ondertitels konden niet in de MP4 gezet worden.\n" + str(exc)) from exc
    if not partial.is_file() or partial.stat().st_size < 32:
        partial.unlink(missing_ok=True)
        raise RuntimeError("De MP4 met ondertitels is leeg.")
    partial.replace(target)
    return target
