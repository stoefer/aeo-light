# -*- coding: utf-8 -*-
"""TeleCine Audio Translator met optionele UVR-stemscheiding.

Starten zonder terminal:
  Windows: dubbelklik installeer.bat (eenmalig), daarna het bureaublad-icoon.
  Linux:   ./installeer.sh (eenmalig), daarna ./start.sh

Vereist: faster-whisper, argostranslate, piper-tts, sounddevice, soundfile, numpy.
Voor UVR: python -m pip install "audio-separator[cpu]"
"""
import hashlib, json, os, queue, re, shutil, subprocess, sys, tempfile, threading, time, wave
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from telecine_media import (
    MediaStopped, extract_wav, ffmpeg_missing_message, find_ffmpeg, is_video_file, probe_video,
    timeline_for_audio, write_mp4, write_subtitled_mp4, extract_frame,
)

APP_TITLE = "TeleCine Audio Translator"
BASE_DIR = Path(__file__).resolve().parent

def read_app_version():
    """Versie uit VERSION, zodat een volgende release hetzelfde nummer verhoogt."""
    try:
        text = (BASE_DIR / "VERSION").read_text(encoding="utf-8").strip()
    except OSError:
        text = ""
    return text or "0.9.01"

APP_VERSION = read_app_version()
VOICE_DIR = BASE_DIR / "voices"
JOB_DIR = BASE_DIR / ".telecine_jobs"
MODEL_DIR = BASE_DIR / "uvr_models"
VOICES = {
    "Nathalie - Vlaams (medium)": "nl_BE-nathalie-medium",
    "RDH - Vlaams, mannelijk (medium)": "nl_BE-rdh-medium",
}
DEFAULT_VOICE_LABEL = "Nathalie - Vlaams (medium)"
UVR_MODEL = "UVR-MDX-NET-Inst_HQ_3.onnx"
LANGUAGES = {"Frans": "fr", "Engels": "en"}

def translation_route_note(source):
    """Korte status als Frans niet rechtstreeks naar het Nederlands gaat."""
    if source == "fr":
        return "Frans wordt via Engels vertaald."
    return ""

APPEARANCES = ("Licht", "Donker")
APPEARANCE_COLORS = {
    "Licht": {
        "bg": "#f4f4f4", "fg": "#1c1c1c", "muted": "#555555", "field": "#ffffff",
        "button": "#e6e6e6", "button_active": "#d5d5d5", "disabled": "#ececec",
        "border": "#b5b5b5", "accent": "#2f6fad", "warning": "#8a3b00", "hint": "#333333",
        "meter": "#202020", "meter_mark": "#555555", "hold": "#ffffff",
    },
    "Donker": {
        "bg": "#1e1e1e", "fg": "#f2f2f2", "muted": "#bdbdbd", "field": "#2c2c2c",
        "button": "#3a3a3a", "button_active": "#4a4a4a", "disabled": "#2a2a2a",
        "border": "#555555", "accent": "#6cb6ff", "warning": "#ffb74d", "hint": "#d7d7d7",
        "meter": "#141414", "meter_mark": "#666666", "hold": "#ffffff",
    },
}
_appearance_colors = APPEARANCE_COLORS["Licht"]

def startup_window_size(screen_w, screen_h, content_h, margin=88):
    """Hoogte en breedte bij de start.

    Het venster opent meteen zo hoog dat de instellingen erin passen. Is het
    scherm korter, dan gebruikt het de vrije hoogte en schuift de rest.
    """
    screen_w = max(1, int(screen_w))
    screen_h = max(1, int(screen_h))
    avail_h = max(520, screen_h - margin)
    width = min(1180, max(640, screen_w - 16))
    height = min(avail_h, max(640, int(content_h)))
    return width, height

def appearance_path():
    return JOB_DIR / "weergave.json"

def load_appearance(path=None):
    path = Path(path) if path is not None else appearance_path()
    try:
        name = json.loads(Path(path).read_text(encoding="utf-8")).get("appearance", "")
    except (OSError, json.JSONDecodeError, AttributeError, TypeError):
        name = ""
    return name if name in APPEARANCES else "Licht"

def save_appearance(name, path=None):
    if name not in APPEARANCES:
        return
    path = Path(path) if path is not None else appearance_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"appearance": name}, ensure_ascii=False) + "\n", encoding="utf-8")

def paint_tk_tree(widget, colors):
    """Kleurt de Tk-onderdelen. ttk volgt de stijl."""
    kind = ""
    try:
        kind = widget.winfo_class()
    except tk.TclError:
        return
    try:
        if kind in ("Tk", "Toplevel"):
            widget.configure(bg=colors["bg"])
        elif kind == "Canvas":
            if getattr(widget, "_meter_canvas", False):
                widget.configure(bg=colors["meter"], highlightbackground=colors["border"])
            else:
                widget.configure(bg=colors["bg"])
        elif kind == "Text":
            widget.configure(bg=colors["field"], fg=colors["fg"], insertbackground=colors["fg"])
        elif kind == "Label":
            widget.configure(bg=colors["bg"], fg=colors["fg"])
        elif kind == "Scrollbar":
            widget.configure(background=colors["button"], troughcolor=colors["field"], activebackground=colors["muted"])
    except tk.TclError:
        pass
    for child in widget.winfo_children():
        paint_tk_tree(child, colors)

WHISPER_MODELS = ["tiny", "base", "small", "medium"]
STEPS = ["Bestand controleren", "Stem scheiden (UVR)", "Spraak herkennen",
         "Naar Nederlands vertalen", "Vlaamse WAV genereren", "Achtergrond mengen", "Resultaten opslaan"]
WEIGHTS = [0.02, 0.28, 0.35, 0.10, 0.17, 0.06, 0.02]

class StoppedByUser(Exception): pass

def dbfs(value):
    """Zet een lineaire amplitude om naar dBFS."""
    import math
    return -120.0 if value <= 0 else max(-120.0, 20.0 * math.log10(float(value)))

def measure_wav_levels(path):
    """Meet absolute peak en RMS van een WAV, kanaalonafhankelijk."""
    try:
        import numpy as np, soundfile as sf
    except ImportError as e:
        raise RuntimeError("Voor Peak/RMS-meting zijn numpy en soundfile nodig. Installeer: python -m pip install numpy soundfile") from e
    peak=0.0; sumsq=0.0; count=0
    with sf.SoundFile(str(path)) as f:
        while True:
            block=f.read(65536,dtype="float32",always_2d=True)
            if len(block)==0: break
            peak=max(peak,float(np.max(np.abs(block))))
            sumsq += float(np.sum(np.square(block,dtype=np.float64)))
            count += int(block.size)
    rms=(sumsq/max(1,count))**0.5
    return {"peak":peak,"rms":rms,"peak_db":dbfs(peak),"rms_db":dbfs(rms),"clipping":peak>=0.999}

class LevelMeter(ttk.Frame):
    """Dubbele horizontale meter met Peak, RMS en Peak Hold."""
    def __init__(self,parent,title):
        super().__init__(parent)
        self.title=title; self.peak_hold=-120.0
        ttk.Label(self,text=title,width=18).grid(row=0,column=0,rowspan=2,sticky="w",padx=(0,6))
        self.peak_canvas=tk.Canvas(self,height=14,bg="#202020",highlightthickness=1,highlightbackground="#666")
        self.rms_canvas=tk.Canvas(self,height=14,bg="#202020",highlightthickness=1,highlightbackground="#666")
        self.peak_canvas._meter_canvas=True
        self.rms_canvas._meter_canvas=True
        self.peak_canvas.grid(row=0,column=1,sticky="ew",pady=1); self.rms_canvas.grid(row=1,column=1,sticky="ew",pady=1)
        self.peak_text=ttk.Label(self,text="Peak  --,- dBFS",width=19); self.rms_text=ttk.Label(self,text="RMS   --,- dBFS",width=19)
        self.peak_text.grid(row=0,column=2,padx=(7,0)); self.rms_text.grid(row=1,column=2,padx=(7,0))
        self.clip=ttk.Label(self,text="",width=9); self.clip.grid(row=0,column=3,rowspan=2,padx=(4,0))
        self.columnconfigure(1,weight=1)
        for c in (self.peak_canvas,self.rms_canvas): c.bind("<Configure>",lambda e:self.redraw())
        self.values=None
    def _x(self,db,width): return max(0,min(width,(db+60.0)/60.0*width))
    def _bar(self,canvas,db,hold=None):
        canvas.delete("all"); w=max(10,canvas.winfo_width()); h=max(8,canvas.winfo_height())
        x=self._x(db,w)
        zones=[(-60,-18,"#2ca25f"),(-18,-6,"#f0c419"),(-6,0,"#d9534f")]
        for lo,hi,color in zones:
            xa=self._x(lo,w); xb=min(x,self._x(hi,w))
            if xb>xa: canvas.create_rectangle(xa,1,xb,h-1,fill=color,outline="")
        mark_color=_appearance_colors["meter_mark"]
        for mark in (-24,-12,-6,-3):
            xm=self._x(mark,w); canvas.create_line(xm,0,xm,h,fill=mark_color)
        if hold is not None and hold>-120:
            xh=self._x(hold,w); canvas.create_line(xh,0,xh,h,fill=_appearance_colors["hold"],width=2)
    def set_levels(self,peak,rms,reset_hold=False):
        self.values=(float(peak),float(rms))
        p=dbfs(peak); r=dbfs(rms)
        if reset_hold: self.peak_hold=p
        else: self.peak_hold=max(self.peak_hold,p)
        self.peak_text.config(text=f"Peak {p:6.1f} dBFS".replace(".", ",")); self.rms_text.config(text=f"RMS  {r:6.1f} dBFS".replace(".", ","))
        self.clip.config(text="CLIP" if peak>=0.999 else "",foreground="#c00000")
        self.redraw()
    def clear(self):
        self.values=None; self.peak_hold=-120.0
        self.peak_text.config(text="Peak  --,- dBFS"); self.rms_text.config(text="RMS   --,- dBFS"); self.clip.config(text="")
        self.redraw()
    def redraw(self):
        if self.values is None:
            self._bar(self.peak_canvas,-120); self._bar(self.rms_canvas,-120); return
        peak,rms=self.values; self._bar(self.peak_canvas,dbfs(peak),self.peak_hold); self._bar(self.rms_canvas,dbfs(rms))

def safe_name(s): return re.sub(r"[^A-Za-z0-9._-]+", "_", s).strip("_") or "audio"
def fmt(sec):
    if sec is None or sec < 0: return "Nog niet beschikbaar"
    h, r = divmod(int(sec), 3600); m, s = divmod(r, 60)
    return f"{h:02d}:{m:02d}:{s:02d}"

def find_espeak_data():
    candidates = []
    if os.environ.get("ESPEAK_DATA_PATH"): candidates.append(Path(os.environ["ESPEAK_DATA_PATH"]))
    for key in ("ProgramFiles", "ProgramFiles(x86)"):
        if os.environ.get(key):
            root = Path(os.environ[key])
            candidates += [root/"eSpeak NG"/"espeak-ng-data", root/"eSpeak-NG"/"espeak-ng-data",
                           root/"eSpeak NG"/"share"/"espeak-ng-data"]
    candidates += [
        Path("/usr/share/espeak-ng-data"),
        Path("/usr/lib/x86_64-linux-gnu/espeak-ng-data"),
        Path("/usr/lib/aarch64-linux-gnu/espeak-ng-data"),
        Path("/usr/local/share/espeak-ng-data"),
        Path("/opt/homebrew/share/espeak-ng-data"),
    ]
    return next((str(p) for p in candidates if (p/"phontab").exists()), None)

def open_folder(path):
    """Open een map in de bestandsverkenner, op Windows, macOS en Linux."""
    folder = Path(path)
    folder.mkdir(parents=True, exist_ok=True)
    target = str(folder)
    if os.name == "nt":
        os.startfile(target)
        return
    opener = "open" if sys.platform == "darwin" else "xdg-open"
    if shutil.which(opener) is None:
        raise FileNotFoundError("Geen bestandsverkenner gevonden (" + opener + "). Map: " + target)
    subprocess.Popen([opener, target], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)

def install_command_hint():
    """Korte aanwijzing die klopt op Windows én Linux."""
    if os.name == "nt":
        return "Voer installeer.bat opnieuw uit."
    return "Voer ./installeer.sh opnieuw uit."

def popen_hidden(cmd, **kwargs):
    """Start een hulpprogramma. Op Windows zonder extra consolevenster."""
    if os.name == "nt":
        kwargs["creationflags"] = kwargs.get("creationflags", 0) | subprocess.CREATE_NO_WINDOW
    return subprocess.Popen(cmd, **kwargs)

def console_python(executable=None):
    """Python voor Piper en andere hulpmiddelen.

    Het bureaublad-icoon start het venster met pythonw.exe. Dat programma heeft
    geen console; Piper krijgt dan geen bruikbare standaarduitvoer. python.exe
    in dezelfde map wel, en popen_hidden verbergt dat venster.
    """
    exe = Path(executable or sys.executable)
    if exe.name.lower() == "pythonw.exe":
        console = exe.with_name("python.exe")
        if console.exists():
            return str(console)
    return str(exe)

def fit_channels(audio, channels):
    """Maak een audioblok exact `channels` kanalen breed."""
    import numpy as np
    if audio.size == 0:
        return np.zeros((audio.shape[0], channels), dtype=audio.dtype)
    current = int(audio.shape[1])
    if current == channels:
        return audio
    if current == 1:
        return np.repeat(audio, channels, axis=1)
    if channels == 1:
        return np.mean(audio, axis=1, keepdims=True).astype(audio.dtype)
    fitted = np.zeros((audio.shape[0], channels), dtype=audio.dtype)
    count = min(current, channels)
    fitted[:, :count] = audio[:, :count]
    return fitted

def read_wav_duration(path):
    try:
        import soundfile as sf
        return float(sf.info(str(path)).duration)
    except Exception:
        with wave.open(str(path), "rb") as w:
            return w.getnframes() / max(1, w.getframerate())

def translate_chunks(text, source, stop, callback):
    import argostranslate.translate as tr
    parts = [p.strip() for p in re.split(r"(?<=[.!?])\s+|\n+", text) if p.strip()]
    chunks=[]; cur=""
    for p in parts:
        q=(cur+" "+p).strip()
        if cur and len(q)>1000: chunks.append(cur); cur=p
        else: cur=q
    if cur: chunks.append(cur)
    out=[]
    for i, c in enumerate(chunks, 1):
        if stop.is_set(): raise StoppedByUser
        out.append(tr.translate(c, source, "nl")); callback(i/max(1,len(chunks)))
    return "\n\n".join(out)

def hhmmss_ms(sec):
    sec=max(0.0,float(sec)); h=int(sec//3600); m=int((sec%3600)//60); ss=sec%60
    return f"{h:02d}:{m:02d}:{ss:06.3f}"

def optional_seconds(raw):
    """Lege tekst is geen punt. Anders seconden."""
    text = str(raw or "").strip()
    if not text:
        return None
    return float(text)

def point_seconds(index, fps, duration, kind):
    """Tijd van een frame. Het eindpunt valt aan het einde van dat frame."""
    step = float(fps) if fps else 25.0
    length = max(0.0, float(duration or 0))
    place = max(0, int(index))
    value = (place + 1) / step if kind == "end" else place / step
    if length > 0:
        value = min(length, value)
    return round(value, 3)

def assign_range_point(start, end, kind, seconds):
    """Zet start of einde. Het einde blijft na de start."""
    seconds = round(float(seconds), 3)
    if kind == "start":
        if end is not None and seconds >= float(end):
            return start, end, "Het startpunt moet voor het eindpunt liggen."
        return seconds, end, ""
    if start is not None and seconds <= float(start):
        return start, end, "Het eindpunt moet na het startpunt liggen."
    return start, seconds, ""

def limit_segments(segments, start, end):
    """Houd de zinnen die in het bereik vallen en kap hun tijden daarop af."""
    if start is None and end is None:
        return [dict(item) for item in segments]
    begin = 0.0 if start is None else float(start)
    finish = None if end is None else float(end)
    kept = []
    for item in segments:
        opening = max(float(item["start"]), begin)
        closing = float(item["end"])
        if finish is not None:
            closing = min(closing, finish)
        if closing - opening < 0.02:
            continue
        copy = dict(item)
        copy["start"] = opening
        copy["end"] = closing
        kept.append(copy)
    return kept

def spoken_text(segment, key="text"):
    """Eén regel tekst. Regeleinden in het bewerkvenster worden spaties."""
    return " ".join(str((segment or {}).get(key) or "").split())

def srt_timestamp(sec):
    """Tijdcode voor SRT, met een komma voor de milliseconden."""
    total_ms = int(round(max(0.0, float(sec)) * 1000))
    hours, remainder = divmod(total_ms, 3_600_000)
    minutes, remainder = divmod(remainder, 60_000)
    seconds, millis = divmod(remainder, 1000)
    return f"{hours:02d}:{minutes:02d}:{seconds:02d},{millis:03d}"

def apply_transcript_edits(segments, texts, key="text"):
    """Zet nieuwe zinnen op de segmenten. Start en einde blijven gelijk.

    key "text" bewerkt de herkende bronzin. key "translation" bewerkt het
    Nederlands en laat de bronzin staan.
    """
    if key not in ("text", "translation"):
        raise ValueError("Onbekend tekstveld.")
    if len(texts) != len(segments):
        raise ValueError("Het aantal zinnen komt niet overeen met de herkende segmenten.")
    edited = []
    for segment, text in zip(segments, texts):
        item = {
            "start": float(segment["start"]),
            "end": float(segment["end"]),
            "text": spoken_text(segment) if key == "translation" else spoken_text({"text": text}),
        }
        if key == "translation":
            item["translation"] = spoken_text({"text": text})
        edited.append(item)
    return edited

def _joined_text(first, second, key):
    return " ".join(part for part in (spoken_text(first, key), spoken_text(second, key)) if part)

def merge_transcript_segments(segments, index, key="text"):
    """Voeg twee opeenvolgende zinnen samen.

    De nieuwe zin begint op de begintijd van de eerste en eindigt op de eindtijd
    van de tweede. Dat zijn allebei tijden uit de herkenning.
    """
    if key not in ("text", "translation"):
        raise ValueError("Onbekend tekstveld.")
    if index < 0 or index + 1 >= len(segments):
        raise IndexError("Er is geen volgende zin om mee samen te voegen.")
    first, second = segments[index], segments[index + 1]
    merged = {
        "start": float(first["start"]),
        "end": float(second["end"]),
        "text": _joined_text(first, second, "text"),
    }
    if key == "translation":
        merged["translation"] = _joined_text(first, second, "translation")
    return [dict(item) for item in segments[:index]] + [merged] + [dict(item) for item in segments[index + 2 :]]

def format_timed_text(segments, key="text"):
    """Tekstbestand met tijdcode. Lege zinnen blijven weg, de andere tijden niet."""
    lines = []
    for segment in segments:
        text = spoken_text(segment, key)
        if not text:
            continue
        lines.append(f"[{hhmmss_ms(segment['start'])} - {hhmmss_ms(segment['end'])}] {text}")
    return "\n".join(lines)

def write_srt(segments, path, text_key="text"):
    """Schrijf ondertitels. Elke cue houdt de begin- en eindtijd van het segment."""
    cues = []
    number = 1
    for segment in segments:
        text = spoken_text(segment, text_key)
        if not text:
            continue
        start = float(segment["start"])
        end = float(segment["end"])
        if end <= start:
            end = start + 0.001
        cues.append(f"{number}\n{srt_timestamp(start)} --> {srt_timestamp(end)}\n{text}\n")
        number += 1
    Path(path).write_text("\n".join(cues) + ("\n" if cues else ""), encoding="utf-8")

_SRT_CLOCK = re.compile(r"^(?P<h>\d{1,3}):(?P<m>\d{2}):(?P<s>\d{2})[,.](?P<ms>\d{1,3})$")

def srt_clock(token):
    """Zet een SRT-tijd om naar seconden. Zowel 00:00:01,250 als 00:00:01.250."""
    match = _SRT_CLOCK.match(token.strip().split()[0])
    if not match:
        raise ValueError("Onbekende tijdcode in het ondertitelbestand: " + token.strip())
    millis = int(match.group("ms")) * (10 ** (3 - len(match.group("ms"))))
    return int(match.group("h")) * 3600 + int(match.group("m")) * 60 + int(match.group("s")) + millis / 1000.0

def read_srt(path):
    """Lees een SRT-bestand als segmenten met start, einde en tekst.

    Lege cues vallen weg. Meerdere regels van één cue worden één zin.
    """
    raw = Path(path).read_text(encoding="utf-8-sig")
    segments = []
    for block in re.split(r"\r?\n\s*\r?\n", raw.strip()):
        lines = [line.strip() for line in block.splitlines() if line.strip()]
        time_at = next((i for i, line in enumerate(lines) if "-->" in line), None)
        if time_at is None:
            continue
        left, right = lines[time_at].split("-->", 1)
        start = srt_clock(left)
        end = srt_clock(right)
        text = spoken_text({"text": " ".join(lines[time_at + 1 :])})
        if not text:
            continue
        if end <= start:
            end = start + 0.001
        segments.append({"start": start, "end": end, "text": text})
    if not segments:
        raise ValueError("Het ondertitelbestand bevat geen zinnen met een tijdcode.")
    return segments

def translate_segments(segments, source, stop, callback):
    import argostranslate.translate as tr
    out = []
    total = max(1, len(segments))
    for i, seg in enumerate(segments, 1):
        if stop.is_set():
            raise StoppedByUser
        item = dict(seg)
        text = spoken_text(seg, "text")
        item["text"] = text
        item["start"] = float(seg["start"])
        item["end"] = float(seg["end"])
        item["translation"] = tr.translate(text, source, "nl").strip() if text else ""
        out.append(item)
        callback(i / total)
    return out

def run_piper(text, output, voice_name, stop, child_setter):
    env=os.environ.copy(); es=find_espeak_data()
    if es: env["ESPEAK_DATA_PATH"]=es
    cmd=[console_python(),"-m","piper","--data-dir",str(VOICE_DIR),"-m",voice_name,"-f",str(output)]
    proc=popen_hidden(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,
                      encoding="utf-8",errors="replace",env=env)
    child_setter(proc)
    try:
        _,err=proc.communicate(input=text)
    finally:
        child_setter(None)
    if stop.is_set(): raise StoppedByUser
    if proc.returncode!=0 or not Path(output).exists():
        extra=""
        if not es:
            if os.name=="nt":
                extra="\n\neSpeak NG is niet gevonden. Installeer het en start het programma opnieuw.\nhttps://github.com/espeak-ng/espeak-ng/releases"
            else:
                extra="\n\neSpeak NG is niet gevonden. Installeer het met:\nsudo apt install espeak-ng"
        raise RuntimeError("De Vlaamse stem kon niet gemaakt worden.\n"+(err or "").strip()+extra)

def build_synchronized_voice(segments, output, temp_dir, voice_name, max_speed_pct, source_duration, stop, callback, child_setter):
    try:
        import numpy as np, soundfile as sf
    except ImportError as e:
        raise RuntimeError("Voor synchronisatie zijn numpy en soundfile nodig. Installeer: python -m pip install numpy soundfile") from e
    temp_dir.mkdir(parents=True,exist_ok=True)
    for f in temp_dir.glob("segment_*.wav"): f.unlink()
    rendered=[]; sample_rate=None; total=max(1,len(segments)); warnings=[]
    for i,seg in enumerate(segments):
        if stop.is_set(): raise StoppedByUser
        phrase = spoken_text(seg, "translation")
        if not phrase:
            callback((i+1)/total)
            continue
        wav=temp_dir/f"segment_{i:05d}.wav"
        run_piper(phrase,wav,voice_name,stop,child_setter)
        audio,sr=sf.read(str(wav),always_2d=True,dtype="float32"); sample_rate=sample_rate or sr
        if sr!=sample_rate:
            old=np.arange(len(audio),dtype=np.float64); n=max(1,round(len(audio)*sample_rate/sr)); new=np.linspace(0,max(0,len(audio)-1),n)
            audio=np.column_stack([np.interp(new,old,audio[:,c]) for c in range(audio.shape[1])]).astype("float32")
        slot=max(0.10,float(seg["end"])-float(seg["start"])); actual=len(audio)/sample_rate
        allowed=1.0+max(0,max_speed_pct)/100.0
        speed=min(allowed,max(1.0,actual/slot))
        if speed>1.001:
            n=max(1,round(len(audio)/speed)); old=np.arange(len(audio),dtype=np.float64); new=np.linspace(0,max(0,len(audio)-1),n)
            audio=np.column_stack([np.interp(new,old,audio[:,c]) for c in range(audio.shape[1])]).astype("float32")
        if len(audio)/sample_rate>slot+0.03: warnings.append(i+1)
        rendered.append((float(seg["start"]),audio)); callback((i+1)/total)
    if sample_rate is None: raise RuntimeError("Er zijn geen Vlaamse segmenten gegenereerd.")
    channels=max(a.shape[1] for _,a in rendered); length=max(round(source_duration*sample_rate),max(round(st*sample_rate)+len(a) for st,a in rendered))
    canvas=np.zeros((length,channels),dtype="float32")
    for start,a in rendered:
        if a.shape[1]==1 and channels>1: a=np.repeat(a,channels,axis=1)
        pos=round(start*sample_rate); end=min(length,pos+len(a)); canvas[pos:end,:a.shape[1]] += a[:end-pos]
    peak=float(np.max(np.abs(canvas))) if canvas.size else 0
    if peak>0.98: canvas*=0.98/peak
    sf.write(str(output),canvas,sample_rate,subtype="PCM_16")
    return warnings

def mix_wavs(background, voice, output, bg_gain=1.0, voice_gain=1.0):
    try:
        import numpy as np, soundfile as sf
    except ImportError as e:
        raise RuntimeError("Voor mengen zijn numpy en soundfile nodig. Installeer: python -m pip install numpy soundfile") from e
    bg, sr_bg = sf.read(str(background), always_2d=True, dtype="float32")
    vc, sr_vc = sf.read(str(voice), always_2d=True, dtype="float32")
    if sr_vc != sr_bg:
        old = np.arange(len(vc), dtype=np.float64)
        new_len = max(1, round(len(vc)*sr_bg/sr_vc))
        new = np.linspace(0, max(0,len(vc)-1), new_len)
        vc = np.column_stack([np.interp(new, old, vc[:,c]) for c in range(vc.shape[1])]).astype("float32")
    channels=max(bg.shape[1], vc.shape[1])
    bg=fit_channels(bg, channels); vc=fit_channels(vc, channels)
    length=max(len(bg),len(vc)); mixed=np.zeros((length,channels),dtype="float32")
    mixed[:len(bg)] += bg*bg_gain; mixed[:len(vc)] += vc*voice_gain
    peak=float(np.max(np.abs(mixed))) if mixed.size else 0
    if peak>0.98: mixed *= 0.98/peak
    sf.write(str(output), mixed, sr_bg, subtype="PCM_16")

class TranscriptEditor(tk.Toplevel):
    """Bewerk de herkende zinnen voordat de vertaling verdergaat.

    De tijdlabels zijn niet bewerkbaar. Alleen de zin zelf verandert, tenzij de
    gebruiker twee opeenvolgende herkende stukken samenvoegt. Samenvoegen houdt
    de begintijd van het eerste stuk en de eindtijd van het tweede.
    """

    def __init__(self, master, segments, on_confirm, on_cancel, audio_path=None, field="text", continue_label=None):
        super().__init__(master)
        if field not in ("text", "translation"):
            raise ValueError("Onbekend tekstveld.")
        self.field = field
        self.editing_translation = field == "translation"
        self.continue_label = continue_label or None
        self.title("Nederlandse vertaling bewerken" if self.editing_translation else "Transcriptie bewerken")
        self.on_confirm = on_confirm
        self.on_cancel = on_cancel
        self.audio_path = Path(audio_path) if audio_path else None
        self._closed = False
        self._history = []
        self._text_widgets = []
        self._source_labels = []
        self.segments = apply_transcript_edits(segments, [spoken_text(item, field) for item in segments], field)
        self.original = [dict(item) for item in self.segments]
        self.protocol("WM_DELETE_WINDOW", self._ask_cancel)
        self.bind("<Escape>", lambda _event: self._ask_cancel())
        self._build()
        self._place()
        self.transient(master)
        self.after(50, self._focus_first)

    def _build(self):
        bar = ttk.Frame(self, padding=(12, 8))
        bar.pack(side="bottom", fill="x")
        continue_label = self.continue_label or ("Spraak starten" if self.editing_translation else "Vertaling starten")
        restore_label = "Oorspronkelijke vertaling" if self.editing_translation else "Oorspronkelijke herkenning"
        self.continue_button = ttk.Button(bar, text=continue_label, command=self._confirm)
        self.continue_button.pack(side="left", padx=(0, 6))
        ttk.Button(bar, text="Stoppen", command=self._ask_cancel).pack(side="left", padx=6)
        self.undo_button = ttk.Button(bar, text="Ongedaan maken", command=self._undo)
        self.undo_button.pack(side="left", padx=6)
        ttk.Button(bar, text=restore_label, command=self._restore).pack(side="left", padx=6)
        self.count_var = tk.StringVar()
        ttk.Label(self, textvariable=self.count_var).pack(side="bottom", anchor="w", padx=12, pady=(0, 4))
        if self.editing_translation and self.continue_label == "Ondertitels plaatsen":
            intro = (
                "Pas de Nederlandse zin aan. De bronzin erboven blijft staan, net als de tijden. "
                "Deze tekst komt als ondertitel in de film, op dezelfde plaats. "
                "Een leeg vak valt weg en schuift de andere zinnen niet op. "
                "Samenvoegen neemt de begintijd van deze zin en de eindtijd van de volgende. "
                "Ctrl+Enter plaatst de ondertitels."
            )
        elif self.editing_translation:
            intro = (
                "Pas de Nederlandse zin aan. De bronzin erboven blijft staan, net als de tijden. "
                "De ondertitel en de Vlaamse stem gebruiken deze tekst op dezelfde plaats. "
                "Een leeg vak wordt niet uitgesproken en schuift de andere zinnen niet op. "
                "Samenvoegen neemt de begintijd van deze zin en de eindtijd van de volgende. "
                "Ctrl+Enter start de spraak."
            )
        else:
            intro = (
                "Pas de zinsbouw aan. De tijden blijven de tijdcode van de herkenning, "
                "zodat de Nederlandse ondertitel en de Vlaamse stem op dezelfde plaats vallen. "
                "Een leeg vak wordt niet uitgesproken en schuift de andere zinnen niet op. "
                "Samenvoegen neemt de begintijd van deze zin en de eindtijd van de volgende. "
                "Ctrl+Enter start de vertaling."
            )
        self.intro = ttk.Label(self, text=intro, wraplength=860, justify="left")
        self.intro.pack(side="top", fill="x", padx=12, pady=(12, 6))
        self.bind("<Configure>", self._wrap_intro, add="+")
        host = ttk.Frame(self)
        host.pack(side="top", fill="both", expand=True, padx=12, pady=(0, 4))
        host.rowconfigure(0, weight=1)
        host.columnconfigure(0, weight=1)
        background = ttk.Style().lookup("TFrame", "background") or self.cget("bg")
        self.canvas = tk.Canvas(host, highlightthickness=0, borderwidth=0, background=background)
        scroll = tk.Scrollbar(host, orient="vertical", command=self.canvas.yview, width=16)
        self.canvas.configure(yscrollcommand=scroll.set)
        self.canvas.grid(row=0, column=0, sticky="nsew")
        scroll.grid(row=0, column=1, sticky="ns")
        self.list_frame = ttk.Frame(self.canvas, padding=(0, 0, 8, 8))
        self.list_window = self.canvas.create_window((0, 0), window=self.list_frame, anchor="nw")
        self.list_frame.bind("<Configure>", self._fit_list)
        self.canvas.bind("<Configure>", self._fit_list)
        self.canvas.bind("<MouseWheel>", self._wheel)
        self.canvas.bind("<Button-4>", self._wheel)
        self.canvas.bind("<Button-5>", self._wheel)
        self.list_frame.bind("<MouseWheel>", self._wheel)
        self.list_frame.bind("<Button-4>", self._wheel)
        self.list_frame.bind("<Button-5>", self._wheel)
        self._rebuild(0)

    def _place(self):
        self.update_idletasks()
        screen_w = max(1, self.winfo_screenwidth())
        screen_h = max(1, self.winfo_screenheight())
        width = min(980, max(640, screen_w - 48))
        height = min(860, max(420, screen_h - 120))
        x = max(0, (screen_w - width) // 2)
        y = max(0, (screen_h - height) // 8)
        self.geometry(f"{width}x{height}+{x}+{y}")
        self.minsize(min(700, screen_w), min(420, height))

    def _wrap_intro(self, event):
        if event.widget is self:
            self.intro.configure(wraplength=max(280, event.width - 36))

    def _fit_list(self, _event=None):
        self.canvas.configure(scrollregion=self.canvas.bbox("all") or (0, 0, 0, 0))
        self.canvas.itemconfigure(self.list_window, width=max(1, self.canvas.winfo_width()))

    def _bind_wheel_tree(self, widget):
        widget.bind("<MouseWheel>", self._wheel)
        widget.bind("<Button-4>", self._wheel)
        widget.bind("<Button-5>", self._wheel)
        for child in widget.winfo_children():
            self._bind_wheel_tree(child)

    def _wheel(self, event):
        try:
            if getattr(event, "delta", 0):
                step = int(-event.delta / 120) or (-1 if event.delta > 0 else 1)
                self.canvas.yview_scroll(step, "units")
            elif getattr(event, "num", None) == 4:
                self.canvas.yview_scroll(-3, "units")
            elif getattr(event, "num", None) == 5:
                self.canvas.yview_scroll(3, "units")
        except tk.TclError:
            pass
        return "break"

    def _focus_first(self):
        if self._text_widgets and not self._closed:
            self._text_widgets[0].focus_set()

    def _rebuild(self, focus_index):
        for child in self.list_frame.winfo_children():
            child.destroy()
        self._text_widgets = []
        self._source_labels = []
        self.list_frame.columnconfigure(0, weight=1)
        can_listen = bool(self.audio_path and self.audio_path.is_file())
        mono = ("TkFixedFont", 10)
        try:
            mono = self.master._mono_font(10)
        except Exception:
            pass
        for index, segment in enumerate(self.segments):
            block = ttk.Frame(self.list_frame, padding=(0, 6, 0, 2))
            block.grid(row=index, column=0, sticky="ew")
            block.columnconfigure(1, weight=1)
            ttk.Label(block, text=f"{index + 1}.", width=4).grid(row=0, column=0, sticky="w")
            times = f"tijd  {hhmmss_ms(segment['start'])}   →   {hhmmss_ms(segment['end'])}"
            tk.Label(block, text=times, anchor="w", font=mono).grid(row=0, column=1, sticky="w")
            actions = ttk.Frame(block)
            actions.grid(row=0, column=2, sticky="e")
            if can_listen:
                ttk.Button(actions, text="Beluister", command=lambda i=index: self._play(i)).pack(side="left", padx=(0, 4))
            if index < len(self.segments) - 1:
                ttk.Button(actions, text="Samenvoegen met volgende", command=lambda i=index: self._merge(i)).pack(side="left")
            text_row = 1
            if self.editing_translation:
                source = spoken_text(segment, "text") or "(lege bronzin)"
                source_label = tk.Label(block, text="bron  " + source, anchor="w", justify="left", fg=_appearance_colors["muted"])
                source_label.grid(row=1, column=0, columnspan=3, sticky="ew", pady=(2, 0))
                self._source_labels.append(source_label)
                block.bind("<Configure>", lambda event, label=source_label: label.configure(wraplength=max(200, event.width - 24)), add="+")
                text_row = 2
            widget = tk.Text(block, height=3, wrap="word", undo=True, font=mono)
            widget.grid(row=text_row, column=0, columnspan=3, sticky="ew", pady=(3, 0))
            widget.insert("1.0", spoken_text(segment, self.field))
            widget.bind("<Control-Return>", self._confirm_event)
            widget.bind("<Control-KP_Enter>", self._confirm_event)
            widget.bind("<Tab>", lambda _event, i=index: self._focus_relative(i, 1))
            widget.bind("<Shift-Tab>", lambda _event, i=index: self._focus_relative(i, -1))
            self._text_widgets.append(widget)
        self._bind_wheel_tree(self.list_frame)
        count = len(self.segments)
        self.count_var.set(f"{count} zin" if count == 1 else f"{count} zinnen")
        self.undo_button.config(state="normal" if self._history else "disabled")
        self._fit_list()
        if self._text_widgets:
            choice = min(max(0, focus_index), len(self._text_widgets) - 1)
            self._text_widgets[choice].focus_set()
        colors = getattr(self.master, "_colors", None)
        if colors:
            try:
                self.configure(bg=colors["bg"])
            except tk.TclError:
                pass
            paint_tk_tree(self, colors)
            for label in self._source_labels:
                try:
                    label.configure(fg=colors["muted"], bg=colors["bg"])
                except tk.TclError:
                    pass

    def _focus_relative(self, index, direction):
        if not self._text_widgets:
            return "break"
        target = self._text_widgets[(index + direction) % len(self._text_widgets)]
        target.focus_set()
        target.see("1.0")
        return "break"

    def _confirm_event(self, _event=None):
        self._confirm()
        return "break"

    def _collect(self):
        texts = [widget.get("1.0", "end") for widget in self._text_widgets]
        edited = apply_transcript_edits(self.segments, texts, self.field)
        self.segments = edited
        return edited

    def _merge(self, index):
        current = self._collect()
        self._history.append(current)
        self.segments = merge_transcript_segments(current, index, self.field)
        self._rebuild(index)

    def _undo(self):
        if not self._history or self._closed:
            return
        self.segments = self._history.pop()
        self._rebuild(0)

    def _restore(self):
        if self._closed:
            return
        self._history.append(self._collect())
        self.segments = [dict(item) for item in self.original]
        self._rebuild(0)

    def _play(self, index):
        current = self._collect()
        segment = current[index]
        start = float(segment["start"])
        end = max(start, float(segment["end"]))
        try:
            import sounddevice as sd
            import soundfile as sf
        except ImportError:
            messagebox.showinfo(APP_TITLE, "Beluisteren vraagt soundfile en sounddevice.\n" + install_command_hint(), parent=self)
            return
        try:
            with sf.SoundFile(str(self.audio_path)) as handle:
                handle.seek(min(len(handle), max(0, round(start * handle.samplerate))))
                frames = max(1, round((end - start) * handle.samplerate))
                data = handle.read(frames, dtype="float32", always_2d=False)
                rate = handle.samplerate
            sd.stop()
            sd.play(data, rate)
        except Exception as exc:
            messagebox.showerror(APP_TITLE, "Dit fragment kon niet worden afgespeeld.\n" + str(exc), parent=self)

    def _stop_audio(self):
        try:
            import sounddevice as sd
            sd.stop()
        except Exception:
            pass

    def _confirm(self):
        if self._closed:
            return
        edited = self._collect()
        if not any(spoken_text(item, self.field) for item in edited):
            if self.editing_translation:
                warning = "Er is geen Nederlandse zin over. Herstel de vertaling, of laat minstens één zin staan."
            else:
                warning = "Er is geen zin over. Herstel de herkenning, of laat minstens één zin staan."
            messagebox.showerror(APP_TITLE, warning, parent=self)
            return
        self._closed = True
        self._stop_audio()
        callback = self.on_confirm
        self.on_confirm = lambda _edited: None
        self.destroy()
        callback(edited)

    def _ask_cancel(self):
        if self._closed:
            return
        question = "De bewerking sluiten en de spraak stoppen?" if self.editing_translation else "De bewerking sluiten en de vertaling stoppen?"
        if not messagebox.askyesno(APP_TITLE, question, parent=self):
            return
        self.close_for_stop()

    def close_for_stop(self):
        if self._closed:
            return
        self._closed = True
        self._stop_audio()
        callback = self.on_cancel
        self.on_cancel = lambda: None
        try:
            self.destroy()
        except tk.TclError:
            pass
        callback()

class FilmPreview(tk.Toplevel):
    """Beeld van de film, met onderaan een schuif per frame.

    Startpunt en eindpunt gebruiken het frame waar de schuif staat. Die tijden
    blijven de tijd van de film, zodat alleen dat stuk vertaald wordt.
    """

    def __init__(self, master, source, timeline, range_start, range_end, on_change):
        super().__init__(master)
        self.title("Film bekijken")
        self.source = Path(source)
        self.timeline = timeline
        self.range_start = range_start
        self.range_end = range_end
        self.on_change = on_change
        self._photo = None
        self._job = 0
        self._results = queue.Queue()
        self._slide_after = None
        self.position = tk.StringVar()
        self.start_text = tk.StringVar()
        self.end_text = tk.StringVar()
        self._build()
        self._refresh_marks()
        self.slider.set(self._index_for(optional_seconds(range_start.get())))
        self._show_position()
        self.protocol("WM_DELETE_WINDOW", self._close)
        self.bind("<Left>", lambda _event: self._step(-1))
        self.bind("<Right>", lambda _event: self._step(1))
        self.after(80, self._poll)
        self.after(60, lambda: self._load_frame(True))
        self.transient(master)

    def _build(self):
        colors = getattr(self.master, "_colors", APPEARANCE_COLORS["Licht"])
        self.configure(bg=colors["bg"])
        slider_row = ttk.Frame(self, padding=(12, 0, 12, 12))
        slider_row.pack(side="bottom", fill="x")
        ttk.Button(slider_row, text="◀", width=3, command=lambda: self._step(-1)).pack(side="left")
        frames = max(1, int(self.timeline["frames"]))
        self.slider = ttk.Scale(slider_row, from_=0, to=max(0, frames - 1), orient="horizontal", command=self._on_slide)
        self.slider.pack(side="left", fill="x", expand=True, padx=8)
        ttk.Button(slider_row, text="▶", width=3, command=lambda: self._step(1)).pack(side="left")
        self.slider.bind("<ButtonRelease-1>", self._on_release)
        marks = ttk.Frame(self, padding=(12, 4, 12, 4))
        marks.pack(side="bottom", fill="x")
        ttk.Button(marks, text="Startpunt", command=self._set_start).pack(side="left")
        ttk.Label(marks, textvariable=self.start_text, width=16).pack(side="left", padx=(6, 16))
        ttk.Button(marks, text="Eindpunt", command=self._set_end).pack(side="left")
        ttk.Label(marks, textvariable=self.end_text, width=16).pack(side="left", padx=(6, 16))
        ttk.Button(marks, text="Wissen", command=self._clear).pack(side="left")
        ttk.Label(self, textvariable=self.position).pack(side="bottom", anchor="w", padx=12, pady=(8, 0))
        self.image_label = tk.Label(
            self, text="Beeld laden..." if self.timeline.get("has_picture") else "Dit bestand heeft geen beeld. Kies het start- en eindpunt op de tijdlijn.",
            bg=colors["field"], fg=colors["fg"], wraplength=680, justify="center",
        )
        self.image_label.pack(side="top", fill="both", expand=True, padx=12, pady=12)
        self.geometry("760x640")

    def _index_for(self, seconds):
        if seconds is None:
            return 0
        fps = float(self.timeline["fps"]) or 1.0
        index = int(round(float(seconds) * fps))
        return max(0, min(int(self.timeline["frames"]) - 1, index))

    def _current_index(self):
        return max(0, min(int(self.timeline["frames"]) - 1, int(round(float(self.slider.get())))))

    def _show_position(self):
        index = self._current_index()
        fps = float(self.timeline["fps"]) or 1.0
        when = point_seconds(index, fps, self.timeline["duration"], "start")
        total = int(self.timeline["frames"])
        if self.timeline.get("has_picture"):
            self.position.set(f"Frame {index + 1} van {total}    {hhmmss_ms(when)}")
        else:
            self.position.set(f"Tijd {hhmmss_ms(when)}    stap {index + 1} van {total}")

    def _on_slide(self, _value=None):
        self._show_position()
        if self._slide_after is not None:
            self.after_cancel(self._slide_after)
        self._slide_after = self.after(180, lambda: self._load_frame(False))

    def _on_release(self, _event=None):
        if self._slide_after is not None:
            self.after_cancel(self._slide_after)
            self._slide_after = None
        self._show_position()
        self._load_frame(True)

    def _step(self, delta):
        self.slider.set(self._current_index() + delta)
        self._on_release()
        return "break"

    def _set_start(self):
        self._store("start")

    def _set_end(self):
        self._store("end")

    def _store(self, kind):
        index = self._current_index()
        seconds = point_seconds(index, self.timeline["fps"], self.timeline["duration"], kind)
        start, end, error = assign_range_point(
            optional_seconds(self.range_start.get()), optional_seconds(self.range_end.get()), kind, seconds,
        )
        if error:
            messagebox.showerror(APP_TITLE, error, parent=self)
            return
        self.range_start.set("" if start is None else f"{start:.3f}")
        self.range_end.set("" if end is None else f"{end:.3f}")
        self._refresh_marks()
        if self.on_change:
            self.on_change()

    def _clear(self):
        self.range_start.set("")
        self.range_end.set("")
        self._refresh_marks()
        if self.on_change:
            self.on_change()

    def _refresh_marks(self):
        start = optional_seconds(self.range_start.get())
        end = optional_seconds(self.range_end.get())
        self.start_text.set("geen" if start is None else hhmmss_ms(start))
        self.end_text.set("geen" if end is None else hhmmss_ms(end))

    def _load_frame(self, accurate):
        if not self.timeline.get("has_picture"):
            return
        self._job += 1
        job = self._job
        seconds = point_seconds(self._current_index(), self.timeline["fps"], self.timeline["duration"], "start")
        target = Path(tempfile.gettempdir()) / f"telecine-frame-{id(self)}.png"

        def work():
            try:
                extract_frame(self.source, seconds, target, accurate=accurate)
                self._results.put((job, target, ""))
            except Exception as exc:
                self._results.put((job, None, str(exc)))

        threading.Thread(target=work, daemon=True).start()

    def _poll(self):
        try:
            while True:
                job, path, error = self._results.get_nowait()
                if job != self._job:
                    continue
                if error or path is None:
                    self.image_label.configure(image="", text=error or "Dit frame kon niet worden getoond.")
                    self._photo = None
                    continue
                try:
                    image = tk.PhotoImage(file=str(path), master=self)
                    step = max(1, (image.width() + 719) // 720)
                    if step > 1:
                        image = image.subsample(step, step)
                    self._photo = image
                    self.image_label.configure(image=image, text="")
                except tk.TclError as exc:
                    self.image_label.configure(image="", text="Dit frame kon niet worden getoond.\n" + str(exc))
        except queue.Empty:
            pass
        try:
            self.after(80, self._poll)
        except tk.TclError:
            pass

    def _close(self):
        self._job += 1
        if self._slide_after is not None:
            try:
                self.after_cancel(self._slide_after)
            except tk.TclError:
                pass
        if getattr(self.master, "_film_preview", None) is self:
            self.master._film_preview = None
        self.destroy()


class App(tk.Tk):
    def __init__(self):
        super().__init__(); self.title(f"{APP_TITLE} {APP_VERSION}"); self.geometry("1180x900"); self.minsize(920,640)
        self.q=queue.Queue(); self.stop_event=threading.Event(); self.edit_ready=threading.Event(); self.worker=None; self.child=None
        self._transcript_editor=None; self._edit_result=None
        self.started=None; self.elapsed_before=0.; self.progress=0.; self.state=None
        self.source=tk.StringVar(); self.outdir=tk.StringVar(value=str(BASE_DIR/"Output"))
        self.language=tk.StringVar(value="Frans"); self.whisper=tk.StringVar(value="small")
        self.voice_choice=tk.StringVar(value=DEFAULT_VOICE_LABEL)
        self.use_uvr=tk.BooleanVar(value=True); self.keep_stems=tk.BooleanVar(value=True)
        self.write_mp4=tk.BooleanVar(value=True)
        self.make_speech=tk.BooleanVar(value=True)
        self.srt_path=tk.StringVar()
        self.range_start=tk.StringVar()
        self.range_end=tk.StringVar()
        self.range_text=tk.StringVar(value="Bereik: de hele film")
        self._film_preview=None
        self._speech_enabled=True
        self._job_running=False
        self.sync_timing=tk.BooleanVar(value=True); self.edit_transcript=tk.BooleanVar(value=True); self.edit_translation=tk.BooleanVar(value=True); self.max_speed=tk.IntVar(value=15)
        self.bg_volume=tk.DoubleVar(value=100); self.voice_volume=tk.DoubleVar(value=100)
        self.bg_volume_text=tk.StringVar(value="100%"); self.voice_volume_text=tk.StringVar(value="100%")
        self.hint=tk.StringVar(value="Installatie controleren...")
        self.level_values={}; self.file_levels={}; self.meters={}; self._env_gen=0; self._env_notes=[]; self._env_ready=False
        # Live afspelen en meteranalyse
        self.play_stream=None; self.play_files={}; self.play_path=None
        self.play_position=0; self.play_total_frames=0; self.play_samplerate=0
        self.play_channels=0; self.play_paused=False; self.play_seeking=False
        self.play_lock=threading.Lock()
        self.play_time=tk.StringVar(value="00:00:00 / 00:00:00")
        self.play_status=tk.StringVar(value="Nog geen eindresultaat gekozen")
        self.play_slider=tk.DoubleVar(value=0.0)
        self.status=tk.StringVar(value="Klaar om te starten"); self.step=tk.StringVar(value="Stap: nog niet gestart")
        self.resume_text=tk.StringVar(value="Hervatten vanaf: niet van toepassing")
        self.percent=tk.StringVar(value="0%"); self.elapsed=tk.StringVar(value="00:00:00"); self.remaining=tk.StringVar(value="Nog niet beschikbaar")
        self.appearance=tk.StringVar(value=load_appearance())
        self._colors=APPEARANCE_COLORS[self.appearance.get()]
        self._auto_place=2
        self.build(); self.apply_appearance(); self._place_on_screen(); self.after(200,self._place_on_screen); self.after(100,self.process_queue); self.after(500,self.clock); self.protocol("WM_DELETE_WINDOW",self.close)
        self.bg_volume.trace_add("write",self._on_volume_change); self.voice_volume.trace_add("write",self._on_volume_change)
        self.voice_choice.trace_add("write",lambda *_:self.refresh_environment_status()); self.use_uvr.trace_add("write",lambda *_:self.refresh_environment_status())
        self.make_speech.trace_add("write",self._sync_speech_controls); self.srt_path.trace_add("write",self._sync_speech_controls)
        self.range_start.trace_add("write",lambda *_:self._refresh_range_label()); self.range_end.trace_add("write",lambda *_:self._refresh_range_label())
        self.show_route_note(); self.refresh_environment_status()

    def _ui_font(self,size,bold=False):
        import tkinter.font as tkfont
        families=set(tkfont.families())
        chosen="TkDefaultFont"
        for name in ("Segoe UI","Ubuntu","Noto Sans","DejaVu Sans","Liberation Sans"):
            if name in families:
                chosen=name; break
        return (chosen,size,"bold") if bold else (chosen,size)

    def _mono_font(self,size):
        import tkinter.font as tkfont
        families=set(tkfont.families())
        for name in ("Consolas","DejaVu Sans Mono","Liberation Mono","Noto Sans Mono"):
            if name in families: return (name,size)
        return ("TkFixedFont",size)

    def build(self):
        """Instellingen schuiven. Meters, speler, knoppen en voortgang blijven staan."""
        style=ttk.Style(); bg=style.lookup("TFrame","background") or self.cget("bg")
        # Knoppen en de melding vast onderaan: die mogen nooit buiten het venster vallen.
        b=ttk.Frame(self,padding=(12,2,12,8)); b.pack(side="bottom",fill="x")
        self.start=ttk.Button(b,text="AUDIO VERTALEN",command=self.start_new); self.start.pack(side="left",padx=5)
        self.stop=ttk.Button(b,text="STOP",command=self.request_stop,state="disabled"); self.stop.pack(side="left",padx=5)
        self.resume=ttk.Button(b,text="HERVATTEN",command=self.resume_job,state="disabled"); self.resume.pack(side="left",padx=5)
        self.open_button=ttk.Button(b,text="UITVOERMAP OPENEN",command=self.open_out); self.open_button.pack(side="left",padx=5)
        self.hint_label=tk.Label(self,textvariable=self.hint,anchor="w",justify="left",bg=bg,fg="#333333",wraplength=900)
        self.hint_label.pack(side="bottom",fill="x",padx=12,pady=(4,0))
        outer=ttk.Frame(self,padding=(12,8,12,4)); outer.pack(side="top",fill="both",expand=True)
        outer.columnconfigure(0,weight=1); outer.rowconfigure(0,weight=1,minsize=72)
        self._outer=outer
        scroll_host=ttk.Frame(outer); scroll_host.grid(row=0,column=0,sticky="nsew")
        scroll_host.rowconfigure(0,weight=1); scroll_host.columnconfigure(0,weight=1)
        canvas=tk.Canvas(scroll_host,highlightthickness=0,borderwidth=0,background=bg)
        scroll=tk.Scrollbar(scroll_host,orient="vertical",command=canvas.yview,width=16,troughcolor="#e4e4e4",background="#5c5c5c",activebackground="#333333")
        canvas.configure(yscrollcommand=scroll.set)
        canvas.grid(row=0,column=0,sticky="nsew"); scroll.grid(row=0,column=1,sticky="ns")
        m=ttk.Frame(canvas,padding=(4,0,8,4)); m.columnconfigure(1,weight=1)
        window=canvas.create_window((0,0),window=m,anchor="nw")
        def fit(_event=None):
            canvas.configure(scrollregion=canvas.bbox("all") or (0,0,0,0))
            canvas.itemconfigure(window,width=max(1,canvas.winfo_width()))
        m.bind("<Configure>",fit); canvas.bind("<Configure>",fit)
        def on_wheel(event):
            try:
                if getattr(event,"delta",0):
                    step=int(-event.delta/120) or (-1 if event.delta>0 else 1)
                    canvas.yview_scroll(step,"units")
                elif getattr(event,"num",None)==4: canvas.yview_scroll(-3,"units")
                elif getattr(event,"num",None)==5: canvas.yview_scroll(3,"units")
            except tk.TclError: pass
        def bind_wheel(widget):
            widget.bind("<MouseWheel>",on_wheel,add="+")
            widget.bind("<Button-4>",on_wheel,add="+")
            widget.bind("<Button-5>",on_wheel,add="+")
            for child in widget.winfo_children(): bind_wheel(child)
        head=ttk.Frame(m); head.grid(row=0,column=0,columnspan=3,sticky="ew",pady=(0,8)); head.columnconfigure(0,weight=1)
        ttk.Label(head,text=f"{APP_TITLE}  {APP_VERSION}",font=self._ui_font(16,True)).grid(row=0,column=0,sticky="w")
        ttk.Label(head,text="Weergave").grid(row=0,column=1,padx=(12,6))
        self.theme_box=ttk.Combobox(head,textvariable=self.appearance,values=list(APPEARANCES),state="readonly",width=10)
        self.theme_box.grid(row=0,column=2,sticky="e")
        self.theme_box.bind("<<ComboboxSelected>>",self.apply_appearance)
        self._settings_frame=m
        ttk.Label(m,text="Bronbestand (WAV of MP4):").grid(row=1,column=0,sticky="w")
        self.source_entry=ttk.Entry(m,textvariable=self.source); self.source_entry.grid(row=1,column=1,sticky="ew",padx=8)
        self.source_button=ttk.Button(m,text="Kiezen...",command=self.choose_source); self.source_button.grid(row=1,column=2)
        preview_row=ttk.Frame(m); preview_row.grid(row=2,column=0,columnspan=3,sticky="ew",pady=(4,0))
        self.preview_button=ttk.Button(preview_row,text="Film bekijken",command=self.open_preview); self.preview_button.pack(side="left")
        ttk.Label(preview_row,textvariable=self.range_text).pack(side="left",padx=12)
        self.mp4_check=ttk.Checkbutton(m,text="Bij een MP4 het beeld behouden en de vertaling als MP4 opslaan",variable=self.write_mp4)
        self.mp4_check.grid(row=3,column=0,columnspan=3,sticky="w",pady=(4,0))
        ttk.Label(m,text="Bronondertitels (SRT, optioneel):").grid(row=4,column=0,sticky="w",pady=(6,0))
        self.srt_entry=ttk.Entry(m,textvariable=self.srt_path); self.srt_entry.grid(row=4,column=1,sticky="ew",padx=8,pady=(6,0))
        srt_buttons=ttk.Frame(m); srt_buttons.grid(row=4,column=2,pady=(6,0))
        self.srt_button=ttk.Button(srt_buttons,text="Kiezen...",command=self.choose_srt); self.srt_button.pack(side="left")
        self.srt_clear=ttk.Button(srt_buttons,text="Wissen",command=self.clear_srt); self.srt_clear.pack(side="left",padx=(6,0))
        self.speech_check=ttk.Checkbutton(m,text="Vlaamse spraak maken",variable=self.make_speech)
        self.speech_check.grid(row=5,column=0,columnspan=3,sticky="w",pady=(6,0))
        ttk.Label(m,text="Zonder spraak komen de Nederlandse ondertitels in de MP4. Beeld en geluid blijven.",wraplength=860).grid(row=6,column=0,columnspan=3,sticky="w")
        ttk.Label(m,text="Uitvoermap:").grid(row=7,column=0,sticky="w",pady=6)
        self.out_entry=ttk.Entry(m,textvariable=self.outdir); self.out_entry.grid(row=7,column=1,sticky="ew",padx=8)
        self.out_button=ttk.Button(m,text="Kiezen...",command=self.choose_out); self.out_button.grid(row=7,column=2)
        opt=ttk.LabelFrame(m,text="Vertaling",padding=8); opt.grid(row=8,column=0,columnspan=3,sticky="ew",pady=(4,6))
        for c in range(3): opt.columnconfigure(c,weight=1)
        ttk.Label(opt,text="Brontaal").grid(row=0,column=0,sticky="w"); ttk.Label(opt,text="Whisper-kwaliteit").grid(row=0,column=1,sticky="w"); ttk.Label(opt,text="Vlaamse stem").grid(row=0,column=2,sticky="w")
        self.lang=ttk.Combobox(opt,textvariable=self.language,values=list(LANGUAGES),state="readonly"); self.lang.grid(row=1,column=0,sticky="ew")
        self.lang.bind("<<ComboboxSelected>>",lambda _e:self.show_route_note())
        self.model=ttk.Combobox(opt,textvariable=self.whisper,values=WHISPER_MODELS,state="readonly"); self.model.grid(row=1,column=1,sticky="ew",padx=8)
        self.voice=ttk.Combobox(opt,textvariable=self.voice_choice,values=list(VOICES),state="readonly"); self.voice.current(0); self.voice.grid(row=1,column=2,sticky="ew")
        self.route_inline=tk.StringVar(value="")
        ttk.Label(opt,textvariable=self.route_inline).grid(row=2,column=0,columnspan=3,sticky="w",pady=(6,0))
        self.edit_check=ttk.Checkbutton(opt,text="Transcriptie bewerken voor de vertaling (tijden blijven staan)",variable=self.edit_transcript)
        self.edit_check.grid(row=3,column=0,columnspan=3,sticky="w",pady=(6,0))
        self.translation_check=ttk.Checkbutton(opt,text="Nederlandse vertaling bewerken voor de spraak (tijden blijven staan)",variable=self.edit_translation)
        self.translation_check.grid(row=4,column=0,columnspan=3,sticky="w",pady=(4,0))
        u=ttk.LabelFrame(m,text="UVR: volumes en losse sporen",padding=8); u.grid(row=9,column=0,columnspan=3,sticky="ew",pady=6)
        u.columnconfigure(1,weight=1)
        self.keep_stems_check=ttk.Checkbutton(u,text="Losse UVR-sporen ook naar de uitvoermap kopiëren",variable=self.keep_stems); self.keep_stems_check.grid(row=0,column=0,columnspan=3,sticky="w",pady=(0,8))
        ttk.Label(u,text="Achtergrondvolume:").grid(row=1,column=0,sticky="w")
        self.bg_scale=ttk.Scale(u,from_=0,to=150,variable=self.bg_volume,orient="horizontal"); self.bg_scale.grid(row=1,column=1,sticky="ew",padx=8)
        ttk.Label(u,textvariable=self.bg_volume_text,width=5).grid(row=1,column=2)
        ttk.Label(u,text="Vlaamse stemvolume:").grid(row=2,column=0,sticky="w")
        self.voice_scale=ttk.Scale(u,from_=0,to=150,variable=self.voice_volume,orient="horizontal"); self.voice_scale.grid(row=2,column=1,sticky="ew",padx=8)
        ttk.Label(u,textvariable=self.voice_volume_text,width=5).grid(row=2,column=2)
        sy=ttk.LabelFrame(m,text="Synchronisatie met originele timing",padding=8); sy.grid(row=10,column=0,columnspan=3,sticky="ew",pady=6)
        sy.columnconfigure(1,weight=1)
        self.sync_check=ttk.Checkbutton(sy,text="Oorspronkelijke starttijden en stiltes behouden",variable=self.sync_timing); self.sync_check.grid(row=0,column=0,columnspan=3,sticky="w")
        ttk.Label(sy,text="Maximale versnelling van Vlaamse segmenten:").grid(row=1,column=0,sticky="w",pady=(6,0))
        self.speed_box=ttk.Combobox(sy,textvariable=self.max_speed,values=[0,5,10,15,20,25,30],state="readonly",width=8); self.speed_box.grid(row=1,column=1,sticky="w",padx=8,pady=(6,0))
        ttk.Label(sy,text="%").grid(row=1,column=2,sticky="w",pady=(6,0))
        bind_wheel(m); bind_wheel(canvas)
        uvr_bar=ttk.Frame(outer); uvr_bar.grid(row=1,column=0,sticky="ew",pady=(6,0))
        self.uvr_check=ttk.Checkbutton(uvr_bar,text="Stem met UVR scheiden en originele achtergrond/muziek behouden",variable=self.use_uvr)
        self.uvr_check.pack(side="left",anchor="w")
        meterbox=ttk.LabelFrame(outer,text="Audioniveaus: Peak, RMS en Peak Hold",padding=8); meterbox.grid(row=2,column=0,sticky="ew",pady=(6,4))
        meterbox.columnconfigure(0,weight=1)
        for row,(key,label) in enumerate((("input","IN origineel"),("background","UVR-achtergrond"),("voice","Vlaamse stem"),("output","OUT eindmix"))):
            meter=LevelMeter(meterbox,label); meter.grid(row=row,column=0,sticky="ew",pady=1); self.meters[key]=meter
        ttk.Label(meterbox,text="Groen: normaal   Geel: hoog   Rood: dicht bij 0 dBFS   Witte lijn: Peak Hold",foreground="#555").grid(row=4,column=0,sticky="w",pady=(4,0))
        player=ttk.LabelFrame(meterbox,text="Resultaat afspelen met live meters",padding=6); player.grid(row=5,column=0,sticky="ew",pady=(8,0)); player.columnconfigure(3,weight=1)
        self.play_button=ttk.Button(player,text="▶ Afspelen",command=self.play_audio); self.play_button.grid(row=0,column=0,padx=(0,5))
        self.pause_button=ttk.Button(player,text="⏸ Pauze",command=self.pause_audio,state="disabled"); self.pause_button.grid(row=0,column=1,padx=5)
        self.play_stop_button=ttk.Button(player,text="⏹ Stop",command=self.stop_audio,state="disabled"); self.play_stop_button.grid(row=0,column=2,padx=5)
        self.timeline=ttk.Scale(player,from_=0,to=100,variable=self.play_slider,orient="horizontal",command=self.preview_seek); self.timeline.grid(row=0,column=3,sticky="ew",padx=8)
        self.timeline.bind("<ButtonPress-1>",self.seek_begin); self.timeline.bind("<ButtonRelease-1>",self.seek_end)
        ttk.Label(player,textvariable=self.play_time).grid(row=1,column=3,sticky="e",padx=(5,0))
        ttk.Label(player,textvariable=self.play_status).grid(row=1,column=0,columnspan=3,sticky="w",pady=(4,0))
        p=ttk.LabelFrame(outer,text="Voortgang",padding=8); p.grid(row=3,column=0,sticky="ew",pady=(4,0)); p.columnconfigure(0,weight=1)
        top=ttk.Frame(p); top.grid(row=0,column=0,sticky="ew"); top.columnconfigure(0,weight=1)
        self.bar=ttk.Progressbar(top,maximum=100); self.bar.grid(row=0,column=0,sticky="ew"); ttk.Label(top,textvariable=self.percent,width=7).grid(row=0,column=1)
        ttk.Label(p,textvariable=self.status,font=self._ui_font(11,True)).grid(row=1,column=0,sticky="w",pady=(6,2)); ttk.Label(p,textvariable=self.step).grid(row=2,column=0,sticky="w"); ttk.Label(p,textvariable=self.resume_text).grid(row=3,column=0,sticky="w")
        t=ttk.Frame(p); t.grid(row=4,column=0,sticky="w",pady=4); ttk.Label(t,text="Verstreken:").grid(row=0,column=0); ttk.Label(t,textvariable=self.elapsed).grid(row=0,column=1,padx=8); ttk.Label(t,text="Resterend:").grid(row=1,column=0); ttk.Label(t,textvariable=self.remaining).grid(row=1,column=1,padx=8)
        log=ttk.Frame(p); log.grid(row=5,column=0,sticky="ew"); log.columnconfigure(0,weight=1)
        self.steps=tk.Text(log,height=7,state="disabled",wrap="none",font=self._mono_font(10))
        step_scroll=tk.Scrollbar(log,command=self.steps.yview,width=14,troughcolor="#e4e4e4",background="#5c5c5c")
        self.steps.configure(yscrollcommand=step_scroll.set)
        self.steps.grid(row=0,column=0,sticky="ew"); step_scroll.grid(row=0,column=1,sticky="ns")
        self.show_steps(0,None)
        self.update_idletasks()
        outer.rowconfigure(1,minsize=uvr_bar.winfo_reqheight())
        outer.rowconfigure(2,minsize=meterbox.winfo_reqheight())
        self._button_bar=b
        self._pinned_height=uvr_bar.winfo_reqheight()+meterbox.winfo_reqheight()+p.winfo_reqheight()+self.hint_label.winfo_reqheight()+b.winfo_reqheight()+48
        self._scroll_canvas=canvas
        self.bind("<Configure>",self._on_window_configure)

    def apply_appearance(self,*_):
        """Zet licht of donker. De keuze blijft bewaard voor de volgende start."""
        global _appearance_colors
        name=self.appearance.get()
        if name not in APPEARANCE_COLORS:
            name="Licht"; self.appearance.set(name)
        colors=APPEARANCE_COLORS[name]
        _appearance_colors=colors
        self._colors=colors
        style=ttk.Style(self)
        style.theme_use("clam")
        style.configure(".",background=colors["bg"],foreground=colors["fg"],fieldbackground=colors["field"],bordercolor=colors["border"],troughcolor=colors["field"])
        style.configure("TFrame",background=colors["bg"])
        style.configure("TLabel",background=colors["bg"],foreground=colors["fg"])
        style.configure("TLabelframe",background=colors["bg"],foreground=colors["fg"],bordercolor=colors["border"])
        style.configure("TLabelframe.Label",background=colors["bg"],foreground=colors["fg"])
        style.configure("TButton",background=colors["button"],foreground=colors["fg"],bordercolor=colors["border"])
        style.map("TButton",background=[("active",colors["button_active"]),("disabled",colors["disabled"])],foreground=[("disabled",colors["muted"])])
        style.configure("TCheckbutton",background=colors["bg"],foreground=colors["fg"])
        style.map("TCheckbutton",background=[("active",colors["bg"])])
        style.configure("TEntry",fieldbackground=colors["field"],foreground=colors["fg"],bordercolor=colors["border"])
        style.configure("TCombobox",fieldbackground=colors["field"],foreground=colors["fg"],background=colors["button"],arrowcolor=colors["fg"],bordercolor=colors["border"])
        style.map("TCombobox",fieldbackground=[("readonly",colors["field"])],foreground=[("readonly",colors["fg"])])
        style.configure("TScrollbar",background=colors["button"],troughcolor=colors["field"],arrowcolor=colors["fg"],bordercolor=colors["border"])
        style.configure("Horizontal.TScale",background=colors["bg"],troughcolor=colors["field"])
        style.configure("Vertical.TScrollbar",background=colors["button"],troughcolor=colors["field"],arrowcolor=colors["fg"])
        style.configure("TProgressbar",background=colors["accent"],troughcolor=colors["field"])
        try: self.configure(bg=colors["bg"])
        except tk.TclError: pass
        paint_tk_tree(self,colors)
        editor=getattr(self,"_transcript_editor",None)
        if editor is not None:
            try: editor.configure(bg=colors["bg"])
            except tk.TclError: pass
            paint_tk_tree(editor,colors)
            for label in getattr(editor,"_source_labels",()):
                try: label.configure(fg=colors["muted"],bg=colors["bg"])
                except tk.TclError: pass
        for meter in getattr(self,"meters",{}).values():
            meter.redraw()
        if hasattr(self,"_refresh_hint"):
            self._refresh_hint()
        save_appearance(name)

    def _on_window_configure(self,event):
        if event.widget is not self: return
        if hasattr(self,"hint_label"):
            self.hint_label.configure(wraplength=max(240,event.width-24))
        self._fit_steps()

    def _fit_steps(self):
        """Maak het stappenlog korter als het venster te laag is. De knoppen blijven staan."""
        if getattr(self,"_fitting",False) or not hasattr(self,"steps"): return
        outer=getattr(self,"_outer",None)
        if outer is None: return
        if self.winfo_height()<200: return
        self._fitting=True
        try:
            for _ in range(4):
                self.update_idletasks()
                limit=outer.winfo_rooty()+outer.winfo_height()-6
                lines=max(1,int(float(self.steps.cget("height"))))
                line_px=max(12,self.steps.winfo_height()/max(1,lines))
                slack=limit-(self.steps.winfo_rooty()+self.steps.winfo_height())
                if slack<-2 and lines>1:
                    drop=max(1,int((-slack+line_px-1)//line_px))
                    self.steps.configure(height=max(1,lines-drop))
                    continue
                if slack>line_px+8 and lines<7:
                    add=min(7-lines,int(slack//line_px))
                    if add:
                        self.steps.configure(height=lines+add)
                        continue
                break
        finally:
            self._fitting=False

    def _content_size(self):
        """Gewenste venstergrootte: instellingen plus de vaste meters en knoppen."""
        self.update_idletasks()
        settings=self._settings_frame.winfo_reqheight() if hasattr(self,"_settings_frame") else 0
        width=self._settings_frame.winfo_reqwidth() if hasattr(self,"_settings_frame") else 1180
        return max(640,width), settings+getattr(self,"_pinned_height",0)

    def _place_on_screen(self):
        """Opent het venster meteen hoog genoeg. Een tweede keer na de eerste layout."""
        if getattr(self,"_auto_place",1)<=0: return
        self._auto_place-=1
        self.update_idletasks()
        screen_w=max(1,self.winfo_screenwidth()); screen_h=max(1,self.winfo_screenheight())
        _content_w,content_h=self._content_size()
        width,height=startup_window_size(screen_w,screen_h,content_h)
        x=max(0,(screen_w-width)//2)
        y=max(0,min(24,(screen_h-88-height)//2))
        self.geometry(f"{width}x{height}+{x}+{y}")
        avail_h=max(520,screen_h-88)
        self.minsize(min(860,screen_w),min(620,avail_h))
        self.maxsize(max(width,screen_w),max(height,avail_h))

    def _on_volume_change(self,*_):
        try:
            self.bg_volume_text.set(f"{int(round(float(self.bg_volume.get())))}%")
            self.voice_volume_text.set(f"{int(round(float(self.voice_volume.get())))}%")
            self.preview_gain_levels()
        except Exception: pass

    def _refresh_hint(self):
        notes=list(self._env_notes)
        route=translation_route_note(LANGUAGES.get(self.language.get(),""))
        if route and route not in notes: notes.append(route)
        if not self._env_ready:
            self.hint.set("Installatie controleren..."+(f" {route}" if route else ""))
            return
        warning=bool(self._env_notes)
        if notes:
            self.hint.set(" · ".join(notes))
        else:
            self.hint.set("Klaar om te starten. Kies een WAV of MP4 en druk op AUDIO VERTALEN.")
        if hasattr(self,"hint_label"):
            colors=getattr(self,"_colors",APPEARANCE_COLORS["Licht"])
            self.hint_label.configure(bg=colors["bg"],fg=colors["warning"] if warning else colors["hint"])
        if self.worker and self.worker.is_alive(): return
        current=self.status.get()
        idle=current.startswith("Klaar om te starten") or current.startswith("Installatie") or current.startswith("Stem") or current.startswith("eSpeak") or current.startswith("Vertaal") or current.startswith("Controle")
        if not idle: return
        if self._env_notes:
            self.status.set(self._env_notes[0])
        elif route:
            self.status.set("Klaar om te starten. "+route)
        else:
            self.status.set("Klaar om te starten.")

    def refresh_environment_status(self):
        if self.worker and self.worker.is_alive(): return
        self._env_gen+=1; gen=self._env_gen
        voice_label=self.voice_choice.get(); use_uvr=bool(self.use_uvr.get()); source_name=self.source.get()
        make_speech=bool(self.make_speech.get()); srt_path=self.srt_path.get().strip()
        def work():
            try: notes=self._environment_notes(voice_label,use_uvr,source_name,make_speech,srt_path)
            except Exception as exc: notes=[f"Controle van de installatie mislukt: {exc}"]
            self.q.put(("env",gen,notes))
        threading.Thread(target=work,daemon=True).start()

    def _environment_notes(self,voice_label,use_uvr,source_name="",make_speech=True,srt_path=""):
        notes=[]
        if srt_path:
            notes.append("Spraakherkenning wordt overgeslagen. De zinnen komen uit het SRT-bestand.")
        video=bool(source_name) and is_video_file(source_name)
        if video and find_ffmpeg() is None:
            notes.append("FFmpeg ontbreekt. Dat is nodig om een MP4 te lezen en terug te schrijven.")
        if not make_speech:
            if video:
                notes.append("Vlaamse spraak staat uit. De Nederlandse ondertitels komen in de film en het geluid blijft.")
            else:
                notes.append("Vlaamse spraak staat uit. Het resultaat is een Nederlands ondertitelbestand.")
        else:
            voice_name=VOICES.get(voice_label)
            if not voice_name:
                notes.append("Kies een Vlaamse stem.")
            else:
                missing=[name for name in (f"{voice_name}.onnx",f"{voice_name}.onnx.json") if not (VOICE_DIR/name).exists()]
                if missing: notes.append("Stem ontbreekt ("+", ".join(missing)+"). "+install_command_hint())
            if find_espeak_data() is None:
                notes.append("eSpeak NG is niet gevonden. Installeer het voor de Vlaamse uitspraak." if os.name=="nt" else "eSpeak NG ontbreekt. Installeer het met: sudo apt install espeak-ng")
            if use_uvr and not self.separator_exe():
                notes.append("Stemscheiding (UVR) is niet geïnstalleerd. Vink die optie uit, of "+install_command_hint()[:1].lower()+install_command_hint()[1:])
        try:
            from telecine_setup import argos_can_translate, installed_translation_pairs
            pairs=installed_translation_pairs()
            missing=[label for label,code in LANGUAGES.items() if not argos_can_translate(code,pairs)]
            if missing:
                notes.append("Vertaalpakket nog niet klaar voor "+" en ".join(missing)+". Dat wordt bij de eerste vertaling gedownload.")
        except Exception:
            notes.append("Vertaalonderdeel ontbreekt. "+install_command_hint())
        return notes

    def set_meter(self,key,values,reset_hold=False,remember=False):
        self.level_values[key]=dict(values)
        if remember: self.file_levels[key]=dict(values)
        if key in self.meters: self.meters[key].set_levels(values["peak"],values["rms"],reset_hold)
    def preview_gain_levels(self):
        """Toont direct het effect van de volumes op achtergrond en stem.
        OUT blijft de laatst werkelijk berekende eindmix."""
        try:
            if "background_raw" in self.level_values:
                g=float(self.bg_volume.get())/100.0; v=self.level_values["background_raw"]
                self.set_meter("background",{"peak":v["peak"]*g,"rms":v["rms"]*g})
            if "voice_raw" in self.level_values:
                g=float(self.voice_volume.get())/100.0; v=self.level_values["voice_raw"]
                self.set_meter("voice",{"peak":v["peak"]*g,"rms":v["rms"]*g})
        except Exception: pass
    def queue_measure(self,key,path,raw_key=None):
        try:
            values=measure_wav_levels(path)
            self.q.put(("meter",key,values,raw_key))
        except Exception as e:
            self.q.put(("meter_error",key,str(e)))
    def playback_tracks(self):
        """Bepaal de vier synchroon te meten WAV-sporen voor de huidige taak."""
        source=self.source.get().strip()
        if not source or not Path(source).is_file(): return {}
        p=self.paths(source); st=self.load(p) or self.state or {}
        final=Path(st.get("output_file","")) if st.get("output_file") else p["mixed"]
        original=p["extracted"] if is_video_file(source) and p["extracted"].is_file() else Path(source)
        tracks={"input":original,"voice":p["voice"],"output":final}
        if st.get("use_uvr",self.use_uvr.get()): tracks["background"]=p["background"]
        return {k:v for k,v in tracks.items() if v and Path(v).is_file()}

    def _close_play_files(self):
        for f in list(self.play_files.values()):
            try:f.close()
            except:pass
        self.play_files={}

    def restore_file_meters(self):
        """Zet de meters terug op de laatst gemeten bestanden, niet op stilte."""
        if not self.file_levels:
            for meter in self.meters.values(): meter.clear()
            return
        for key,values in self.file_levels.items():
            self.set_meter(key,values,reset_hold=True)
        self.preview_gain_levels()

    def _block_levels(self,data):
        import numpy as np
        if data is None or getattr(data,"size",0)==0:return {"peak":0.0,"rms":0.0}
        return {"peak":float(np.max(np.abs(data))),"rms":float(np.sqrt(np.mean(np.square(data,dtype=np.float64))))}

    def _read_track_at(self,key,start_sec,duration):
        import numpy as np
        f=self.play_files.get(key)
        if not f:return np.zeros((0,1),dtype="float32")
        pos=max(0,round(start_sec*f.samplerate)); frames=max(1,round(duration*f.samplerate))
        try:
            f.seek(min(pos,len(f))); return f.read(frames,dtype="float32",always_2d=True)
        except:return np.zeros((0,max(1,getattr(f,"channels",1))),dtype="float32")

    def play_audio(self):
        try:
            import sounddevice as sd, soundfile as sf
        except ImportError:
            messagebox.showerror(APP_TITLE,'Voor live afspelen ontbreekt sounddevice. Voer in de actieve venv uit:\n\npython -m pip install sounddevice')
            return
        tracks=self.playback_tracks(); output=tracks.get("output")
        if not output or not Path(output).is_file():
            messagebox.showinfo(APP_TITLE,"Maak eerst een Vlaamse eindmix of hervat een voltooide taak."); return
        if self.play_stream and getattr(self.play_stream,"active",False):
            self.play_paused=False; self.pause_button.config(text="⏸ Pauze"); self.play_status.set("Afspelen hervat"); return
        self.stop_audio(reset_position=False)
        try:
            self.play_files={k:sf.SoundFile(str(v)) for k,v in tracks.items()}
            out_f=self.play_files["output"]; self.play_path=Path(output); self.play_samplerate=int(out_f.samplerate)
            self.play_channels=int(out_f.channels); self.play_total_frames=len(out_f)
            self.play_position=min(self.play_position,max(0,self.play_total_frames-1)); self.play_paused=False
            def callback(outdata,frames,time_info,status):
                import numpy as np, sounddevice as _sd
                with self.play_lock:
                    if self.play_paused:
                        outdata.fill(0); return
                    start=self.play_position
                    if start>=self.play_total_frames:
                        outdata.fill(0); self.q.put(("play_finished",)); raise _sd.CallbackStop
                    self.play_files["output"].seek(start)
                    block=self.play_files["output"].read(frames,dtype="float32",always_2d=True)
                    n=len(block); outdata.fill(0)
                    if n:
                        if block.shape[1]==1 and self.play_channels>1:block=np.repeat(block,self.play_channels,axis=1)
                        outdata[:n,:]=block[:,:self.play_channels]
                    start_sec=start/self.play_samplerate; duration=max(1,n)/self.play_samplerate
                    levels={"output":self._block_levels(block)}
                    for key in ("input","background","voice"):
                        levels[key]=self._block_levels(self._read_track_at(key,start_sec,duration))
                    self.play_position += n
                    self.q.put(("live_levels",levels,self.play_position,self.play_total_frames,self.play_samplerate))
                    if n<frames:
                        self.q.put(("play_finished",)); raise _sd.CallbackStop
            self.play_stream=sd.OutputStream(samplerate=self.play_samplerate,channels=self.play_channels,dtype="float32",callback=callback,finished_callback=lambda:self.q.put(("play_finished",)))
            self.play_stream.start(); self.pause_button.config(state="normal",text="⏸ Pauze"); self.play_stop_button.config(state="normal")
            self.play_status.set("Live afspelen: IN, achtergrond, stem en OUT worden gemeten")
        except Exception as e:
            self.stop_audio(); messagebox.showerror(APP_TITLE,"Afspelen kon niet starten:\n"+str(e))

    def pause_audio(self):
        if not self.play_stream:return
        self.play_paused=not self.play_paused
        self.pause_button.config(text="▶ Hervatten" if self.play_paused else "⏸ Pauze")
        self.play_status.set("Afspelen gepauzeerd" if self.play_paused else "Afspelen hervat")

    def stop_audio(self,reset_position=True):
        stream=self.play_stream; self.play_stream=None
        if stream:
            try:stream.abort(); stream.close()
            except:pass
        self._close_play_files(); self.play_paused=False
        if reset_position:
            self.play_position=0; self.play_slider.set(0)
            total=self.play_total_frames/self.play_samplerate if self.play_samplerate else 0
            self.play_time.set("00:00:00 / "+(fmt(total) if total else "00:00:00")); self.restore_file_meters()
            self.play_status.set("Afspelen gestopt")
        self.pause_button.config(state="disabled",text="⏸ Pauze"); self.play_stop_button.config(state="disabled")

    def seek_begin(self,event=None): self.play_seeking=True
    def preview_seek(self,value=None):
        if self.play_seeking and self.play_samplerate and self.play_total_frames:
            sec=float(self.play_slider.get())/100*self.play_total_frames/self.play_samplerate
            self.play_time.set(f"{fmt(sec)} / {fmt(self.play_total_frames/self.play_samplerate)}")
    def seek_end(self,event=None):
        self.play_seeking=False
        if self.play_total_frames:
            with self.play_lock:self.play_position=round(float(self.play_slider.get())/100*self.play_total_frames)
        self.play_status.set("Nieuwe afspeelpositie ingesteld")

    def choose_source(self):
        f=filedialog.askopenfilename(title="Kies WAV of MP4",filetypes=[("WAV of MP4","*.wav *.mp4 *.m4v"),("WAV-audio","*.wav"),("MP4-video","*.mp4 *.m4v"),("Alle bestanden","*.*")])
        if not f: return
        self.file_levels.clear(); self.level_values.clear(); self.stop_audio()
        self.source.set(f); self.play_status.set("Nog geen eindresultaat gekozen"); self._close_preview(); self.inspect_resume(); self.refresh_environment_status()
    def choose_out(self):
        f=filedialog.askdirectory(title="Kies uitvoermap")
        if f: self.outdir.set(f)
    def open_preview(self):
        raw=self.source.get().strip()
        path=Path(raw) if raw else None
        if path is None or not path.is_file():
            messagebox.showerror(APP_TITLE,"Kies eerst een WAV- of MP4-bestand."); return
        try:
            if is_video_file(path):
                if find_ffmpeg() is None:
                    messagebox.showerror(APP_TITLE,ffmpeg_missing_message()); return
                timeline=probe_video(path)
            else:
                timeline=timeline_for_audio(read_wav_duration(path))
        except Exception as exc:
            messagebox.showerror(APP_TITLE,str(exc)); return
        current=self._film_preview
        if current is not None:
            try:
                if current.winfo_exists() and current.source==path.resolve():
                    current.lift(); return
                current._close()
            except tk.TclError:
                self._film_preview=None
        self._film_preview=FilmPreview(self,path.resolve(),timeline,self.range_start,self.range_end,self._refresh_range_label)
    def _close_preview(self):
        current=self._film_preview
        if current is None: return
        try:
            if current.winfo_exists(): current._close()
        except tk.TclError:
            self._film_preview=None
    def _refresh_range_label(self):
        if not hasattr(self,"range_text"): return
        start=optional_seconds(self.range_start.get()); end=optional_seconds(self.range_end.get())
        if start is None and end is None:
            self.range_text.set("Bereik: de hele film"); return
        left="begin" if start is None else hhmmss_ms(start)
        right="einde" if end is None else hhmmss_ms(end)
        self.range_text.set(f"Bereik: {left}  –  {right}")
    def choose_srt(self):
        f=filedialog.askopenfilename(title="Kies bronondertitels",filetypes=[("SubRip-ondertitels","*.srt"),("Alle bestanden","*.*")])
        if f: self.srt_path.set(f)
    def clear_srt(self):
        self.srt_path.set("")
    def _sync_speech_controls(self,*_):
        """Zonder spraak of met een SRT vallen de stappen weg die dan niet lopen."""
        if not hasattr(self,"model"): return
        if getattr(self,"_job_running",False): return
        speech=bool(self.make_speech.get())
        has_srt=bool(self.srt_path.get().strip())
        self.model.config(state="readonly" if speech and not has_srt else "disabled")
        for widget in (self.voice,self.speed_box):
            widget.config(state="readonly" if speech else "disabled")
        for widget in (self.uvr_check,self.keep_stems_check,self.sync_check,self.bg_scale,self.voice_scale):
            widget.config(state="normal" if speech else "disabled")
        self.refresh_environment_status()
    def open_out(self):
        try:
            open_folder(self.outdir.get() or BASE_DIR/"Output")
        except Exception as e:
            messagebox.showerror(APP_TITLE, "De uitvoermap kon niet geopend worden:\n"+str(e))
    def paths(self, source):
        s=Path(source).resolve(); sig=f"{s}|{s.stat().st_mtime_ns}|{s.stat().st_size}"; jid=hashlib.sha256(sig.encode()).hexdigest()[:16]; d=JOB_DIR/f"{safe_name(s.stem)}_{jid}"
        return {"folder":d,"state":d/"state.json","transcript":d/"transcript.txt","translation":d/"translation_nl.txt","extracted":d/"bron_audio.wav","vocals":d/"uvr_vocals.wav","background":d/"uvr_background.wav","voice":d/"vlaamse_stem.wav","mixed":d/"vlaams_met_achtergrond.wav","uvr_out":d/"uvr_output","segments":d/"segments.json","translated_segments":d/"segments_nl.json","segment_audio":d/"segment_audio"}
    def default_state(self,s):
        return {"source":str(Path(s).resolve()),"source_language":LANGUAGES.get(self.language.get(),"fr"),"model":self.whisper.get(),"voice":VOICES[self.voice_choice.get()],"voice_label":self.voice_choice.get(),"use_uvr":bool(self.use_uvr.get()),"keep_stems":bool(self.keep_stems.get()),"write_mp4":bool(self.write_mp4.get()),"make_speech":bool(self.make_speech.get()),"srt_path":self.srt_path.get().strip(),"range_start":self.range_start.get().strip(),"range_end":self.range_end.get().strip(),"bg_volume":int(round(float(self.bg_volume.get()))),"voice_volume":int(round(float(self.voice_volume.get()))),"sync_timing":bool(self.sync_timing.get()),"edit_transcript":bool(self.edit_transcript.get()),"edit_translation":bool(self.edit_translation.get()),"max_speed":int(self.max_speed.get()),"completed_steps":0,"elapsed":0.,"status":"nieuw","output_dir":str(Path(self.outdir.get() or BASE_DIR/"Output").resolve()),"output_file":"","output_video":"","subtitle_file":"","last_error":""}
    def load(self,p):
        try:return json.loads(p["state"].read_text(encoding="utf-8"))
        except:return None
    def save(self,p,s):
        p["folder"].mkdir(parents=True,exist_ok=True); tmp=p["state"].with_suffix(".tmp"); tmp.write_text(json.dumps(s,ensure_ascii=False,indent=2),encoding="utf-8"); tmp.replace(p["state"])
    def validate(self):
        s=Path(self.source.get().strip())
        if not s.is_file(): messagebox.showerror(APP_TITLE,"Kies eerst een WAV- of MP4-bestand."); return None
        if s.suffix.lower() not in {".wav",".mp4",".m4v"}:
            messagebox.showerror(APP_TITLE,"Kies een WAV- of MP4-bestand."); return None
        if is_video_file(s) and find_ffmpeg() is None:
            messagebox.showerror(APP_TITLE,ffmpeg_missing_message()); return None
        if not self._accept_srt(required=not self.make_speech.get()): return None
        try:
            start=optional_seconds(self.range_start.get()); end=optional_seconds(self.range_end.get())
        except ValueError:
            messagebox.showerror(APP_TITLE,"Het start- of eindpunt is geen geldige tijd."); return None
        if start is not None and end is not None and end<=start:
            messagebox.showerror(APP_TITLE,"Het eindpunt moet na het startpunt liggen."); return None
        if not self.make_speech.get(): return s
        voice_name=VOICES.get(self.voice_choice.get())
        if not voice_name:
            messagebox.showerror(APP_TITLE,"Kies eerst een geldige Vlaamse stem."); return None
        missing=[]
        if not (VOICE_DIR/f"{voice_name}.onnx").exists(): missing.append(f"{voice_name}.onnx")
        if not (VOICE_DIR/f"{voice_name}.onnx.json").exists(): missing.append(f"{voice_name}.onnx.json")
        if missing:
            messagebox.showerror(APP_TITLE,"De gekozen Piper-stem ontbreekt in de map voices:\n\n"+"\n".join(missing)+"\n\n"+install_command_hint()); return None
        if self.use_uvr.get() and not self.separator_exe():
            messagebox.showerror(APP_TITLE,"Stemscheiding (UVR) is niet geïnstalleerd.\n\nVink \"Stem met UVR scheiden\" uit, of:\n"+install_command_hint()); return None
        return s
    def _accept_srt(self, required):
        raw=self.srt_path.get().strip()
        if not raw:
            if required:
                messagebox.showerror(APP_TITLE,"Kies een SRT-bestand, of zet Vlaamse spraak aan.")
            return not required
        path=Path(raw)
        if not path.is_file() or path.suffix.lower()!=".srt":
            messagebox.showerror(APP_TITLE,"Kies een bestaand .srt-bestand."); return False
        try:
            read_srt(path)
        except (OSError, UnicodeError, ValueError) as exc:
            messagebox.showerror(APP_TITLE,"Het ondertitelbestand kan niet gelezen worden.\n"+str(exc)); return False
        return True
    def separator_exe(self):
        options=[Path(sys.executable).parent/"audio-separator.exe",Path(sys.executable).parent/"audio-separator",shutil.which("audio-separator")]
        return next((str(x) for x in options if x and Path(x).exists()),None)
    def start_new(self):
        s=self.validate()
        if not s:return
        p=self.paths(s); old=self.load(p)
        if old and old.get("completed_steps",0)>0 and not messagebox.askyesno(APP_TITLE,"Tussenresultaten gevonden. Helemaal opnieuw beginnen?"): self.inspect_resume(); return
        if p["folder"].exists(): shutil.rmtree(p["folder"])
        self.launch(p,self.default_state(s))
    def inspect_resume(self):
        s=self.source.get().strip()
        if not s or not Path(s).is_file():
            self.resume.config(state="disabled"); return
        st=self.load(self.paths(s))
        if not st:
            self.resume.config(state="disabled"); self.resume_text.set("Hervatten vanaf: niet van toepassing"); self.measure_known_tracks(); return
        n=int(st.get("completed_steps",0)); self.state=st
        if 0<n<len(STEPS):
            self.resume.config(state="normal"); self.resume_text.set(f"Hervatten vanaf stap {n+1}: {STEPS[n]}"); self.set_progress(sum(WEIGHTS[:n])*100); self.show_steps(n,"paused")
        elif n>=len(STEPS):
            self.resume.config(state="disabled"); self.resume_text.set("Deze bron is al volledig verwerkt"); self.set_progress(100); self.show_steps(len(STEPS),"completed")
            if st.get("output_file") and Path(st["output_file"]).is_file():
                self.play_status.set("Eerdere eindmix gevonden. Je kunt die hieronder afspelen.")
            elif st.get("output_video") and Path(st["output_video"]).is_file():
                self.play_status.set("Eerdere film met ondertitels gevonden.")
            elif st.get("subtitle_file") and Path(st["subtitle_file"]).is_file():
                self.play_status.set("Eerdere Nederlandse ondertitels gevonden.")
            else:
                self.play_status.set("De taak is voltooid, maar de eindmix is niet gevonden.")
        else:
            self.resume.config(state="disabled")
        self.measure_known_tracks()
    def measure_known_tracks(self):
        tracks=self.playback_tracks()
        def work():
            for key,path in tracks.items():
                self.queue_measure(key,path,{"background":"background_raw","voice":"voice_raw"}.get(key))
        if tracks: threading.Thread(target=work,daemon=True).start()
    def apply_job_state(self,st):
        label=next((name for name,code in LANGUAGES.items() if code==st.get("source_language")),None)
        if label: self.language.set(label)
        if st.get("model") in WHISPER_MODELS: self.whisper.set(st["model"])
        voice_label=st.get("voice_label")
        if voice_label not in VOICES:
            voice_label=next((name for name,key in VOICES.items() if key==st.get("voice")),None)
        if voice_label in VOICES: self.voice_choice.set(voice_label)
        self.use_uvr.set(bool(st.get("use_uvr",False))); self.keep_stems.set(bool(st.get("keep_stems",True))); self.sync_timing.set(bool(st.get("sync_timing",True))); self.edit_transcript.set(bool(st.get("edit_transcript",False))); self.edit_translation.set(bool(st.get("edit_translation",False)))
        if "write_mp4" in st: self.write_mp4.set(bool(st.get("write_mp4")))
        self.srt_path.set(st.get("srt_path") or "")
        self.make_speech.set(bool(st.get("make_speech", True)))
        self.range_start.set("" if st.get("range_start") in (None,"") else f"{float(st.get('range_start')):.3f}")
        self.range_end.set("" if st.get("range_end") in (None,"") else f"{float(st.get('range_end')):.3f}")
        try:
            self.bg_volume.set(float(st.get("bg_volume",100))); self.voice_volume.set(float(st.get("voice_volume",100)))
            speed=int(st.get("max_speed",15))
            if speed in (0,5,10,15,20,25,30): self.max_speed.set(speed)
        except (TypeError,ValueError,tk.TclError): pass
        if st.get("output_dir"): self.outdir.set(st["output_dir"])
        self.show_route_note()
    def resume_job(self):
        raw=self.source.get().strip()
        if not raw or not Path(raw).is_file():
            messagebox.showerror(APP_TITLE,"Kies eerst een WAV- of MP4-bestand."); return
        p=self.paths(raw); st=self.load(p)
        if not st or st.get("completed_steps",0)>=len(STEPS): messagebox.showinfo(APP_TITLE,"Geen gestopte taak gevonden."); return
        self.apply_job_state(st)
        if not self.validate(): return
        self.launch(p,st)
    def launch(self,p,st):
        if self.worker and self.worker.is_alive():return
        self._close_preview(); self.stop_event.clear(); self.started=time.monotonic(); self.elapsed_before=float(st.get("elapsed",0)); self.state=st; self.progress=sum(WEIGHTS[:st.get("completed_steps",0)])*100; self.running(True)
        self.worker=threading.Thread(target=self.pipeline,args=(p,st),daemon=True); self.worker.start()
    def running(self,on):
        self._job_running=bool(on)
        self.start.config(state="disabled" if on else "normal"); self.stop.config(state="normal" if on else "disabled")
        if on: self.resume.config(state="disabled")
        for widget in (self.source_entry,self.out_entry,self.srt_entry,self.source_button,self.out_button,self.preview_button,self.srt_button,self.srt_clear,self.uvr_check,self.keep_stems_check,self.sync_check,self.mp4_check,self.speech_check,self.edit_check,self.translation_check,self.bg_scale,self.voice_scale):
            widget.config(state="disabled" if on else "normal")
        for widget in (self.lang,self.model,self.voice,self.speed_box):
            widget.config(state="disabled" if on else "readonly")
        if not on: self._sync_speech_controls()
    def _stop_child(self):
        proc=self.child
        if not proc or proc.poll() is not None: return
        try: proc.terminate()
        except Exception: pass
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            try: proc.kill()
            except Exception: pass
        except Exception: pass
    def request_stop(self):
        self.stop_event.set(); self.status.set("Stop aangevraagd..."); self.stop.config(state="disabled")
        editor=getattr(self,"_transcript_editor",None)
        if editor is not None:
            editor.close_for_stop()
        threading.Thread(target=self._stop_child,daemon=True).start()
    def check_stop(self):
        if self.stop_event.is_set():raise StoppedByUser
    def show_route_note(self):
        """Toon dat Frans via Engels gaat, naast de taalkeuze en in de status."""
        note=translation_route_note(LANGUAGES.get(self.language.get(),""))
        if hasattr(self,"route_inline"): self.route_inline.set(note)
        self._refresh_hint()
    def stage(self,i,f,msg):
        f=max(0,min(1,float(f))); val=(sum(WEIGHTS[:i])+WEIGHTS[i]*f)*100; self.progress=val; self.q.put(("progress",val,i,msg))
    def _wait_for_edit(self, payload, audio_path, kind, step, message, continue_label=""):
        """Wacht op het bewerkvenster. De tijden in payload blijven de herkende tijden."""
        self.edit_ready.clear()
        self._edit_result=None
        self.stage(step,0,message)
        audio="" if audio_path is None else str(audio_path)
        self.q.put((kind,payload,audio,continue_label or ""))
        while not self.edit_ready.wait(0.2):
            if self.stop_event.is_set():
                raise StoppedByUser
        if self.stop_event.is_set() or self._edit_result is None:
            raise StoppedByUser
        return self._edit_result
    def wait_for_transcript_edit(self, segments, audio_path):
        """Wacht tot het venster de gecorrigeerde bronzinnen teruggeeft."""
        payload=[{"start":float(item["start"]),"end":float(item["end"]),"text":spoken_text(item)} for item in segments]
        return self._wait_for_edit(payload, audio_path, "edit_transcript", 3, f"Bewerk de {len(payload)} herkende zinnen. De tijden blijven staan.")
    def wait_for_translation_edit(self, segments, audio_path):
        """Wacht tot het venster de gecorrigeerde Nederlandse zinnen teruggeeft."""
        payload=[]
        for item in segments:
            payload.append({"start":float(item["start"]),"end":float(item["end"]),"text":spoken_text(item,"text"),"translation":spoken_text(item,"translation")})
        label="Ondertitels plaatsen" if not getattr(self,"_speech_enabled",True) else ""
        return self._wait_for_edit(payload, audio_path, "edit_translation", 4, f"Bewerk de {len(payload)} Nederlandse zinnen. De tijden blijven staan.", label)
    def _open_transcript_editor(self, segments, audio_path, field="text", continue_label=None):
        if self.stop_event.is_set() or not (self.worker and self.worker.is_alive()):
            self._edit_result=None
            self.edit_ready.set()
            return
        if self._transcript_editor is not None:
            return
        def confirm(edited):
            self._transcript_editor=None
            self._edit_result=edited
            self.edit_ready.set()
        def cancel():
            self._transcript_editor=None
            self._edit_result=None
            self.stop_event.set()
            self.edit_ready.set()
        try:
            self._transcript_editor=TranscriptEditor(self, segments, confirm, cancel, audio_path or None, field, continue_label or None)
        except Exception as exc:
            self._transcript_editor=None
            self._edit_result=None
            self.stop_event.set()
            self.edit_ready.set()
            messagebox.showerror(APP_TITLE, "Het bewerkvenster kon niet worden geopend.\n"+str(exc))
    def finish(self,p,st,n):
        self.check_stop(); st["completed_steps"]=n; st["elapsed"]=self.elapsed_now(); st["status"]="bezig"; self.save(p,st); self.stage(n-1,1,f"Voltooid: {STEPS[n-1]}")
    def elapsed_now(self):return self.elapsed_before+(time.monotonic()-self.started if self.started else 0)

    def work_audio(self,p,st):
        """WAV om te vertalen. Bij een MP4 is dat het uitgehaalde audiospoor."""
        source=Path(st["source"])
        if not is_video_file(source):
            return source
        extracted=p["extracted"]
        if extracted.is_file() and extracted.stat().st_size>=44:
            return extracted
        self.stage(0,.35,"Audio uit MP4 halen...")
        extract_wav(source,extracted,self.stop_event,lambda proc:setattr(self,"child",proc))
        st["audio_wav"]=str(extracted); self.save(p,st)
        return extracted

    def pipeline(self,p,st):
        try:
            p["folder"].mkdir(parents=True,exist_ok=True); source=Path(st["source"]); done=int(st.get("completed_steps",0))
            make_speech=bool(st.get("make_speech",True)); self._speech_enabled=make_speech
            srt_path=(st.get("srt_path") or "").strip()
            use_uvr=bool(st.get("use_uvr",False)) and make_speech
            audio=self.work_audio(p,st) if (make_speech or not srt_path) else None
            if done<1:
                self.stage(0,.2,"Bestand en installatie controleren..."); self.check_stop()
                if not source.exists():raise FileNotFoundError(source)
                if audio is not None: self.queue_measure("input",audio)
                self.finish(p,st,1); done=1
            if done<2:
                if not use_uvr:
                    self.stage(1,1,"UVR overgeslagen."); self.finish(p,st,2); done=2
                else:
                    exe=self.separator_exe()
                    if not exe: raise RuntimeError("Stemscheiding (UVR) is niet geïnstalleerd.\nVink de optie uit, of "+install_command_hint())
                    self.stage(1,.02,"UVR-model laden en stem van achtergrond scheiden..."); p["uvr_out"].mkdir(exist_ok=True); MODEL_DIR.mkdir(exist_ok=True)
                    for f in p["uvr_out"].glob("*"):
                        if f.is_file(): f.unlink()
                    cmd=[exe,str(audio),"--model_filename",UVR_MODEL,"--output_format","WAV","--output_dir",str(p["uvr_out"]),"--model_file_dir",str(MODEL_DIR)]
                    self.child=popen_hidden(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding="utf-8",errors="replace")
                    uvr_out=self.child.stdout; lines=[]
                    def _read_uvr():
                        try:
                            for line in uvr_out: lines.append(line)
                        except Exception: pass
                    reader=threading.Thread(target=_read_uvr,daemon=True); reader.start()
                    while self.child.poll() is None:
                        if self.stop_event.is_set():
                            self._stop_child(); raise StoppedByUser
                        self.stage(1,.35,"UVR is bezig; dit kan op CPU lang duren..."); time.sleep(.2)
                    reader.join(timeout=2); rc=self.child.returncode; self.child=None
                    if self.stop_event.is_set(): raise StoppedByUser
                    if rc!=0: raise RuntimeError("UVR kon de audio niet scheiden.\n"+"".join(lines)[-2500:])
                    files=list(p["uvr_out"].glob("*.wav")); vocals=next((f for f in files if "vocal" in f.name.lower()),None); instr=next((f for f in files if any(x in f.name.lower() for x in ("instrumental","no_vocal","karaoke"))),None)
                    if not vocals or not instr: raise RuntimeError("UVR-uitvoer bevat niet zowel Vocals als Instrumental: "+", ".join(f.name for f in files))
                    shutil.copy2(vocals,p["vocals"]); shutil.copy2(instr,p["background"]); self.queue_measure("background",p["background"],"background_raw"); self.stage(1,1,"UVR-scheiding voltooid."); self.finish(p,st,2); done=2
            recognition_source=p["vocals"] if use_uvr else audio
            if done<3:
                if srt_path:
                    self.stage(2,0,"Ondertitels inlezen...")
                    srt_file=Path(srt_path)
                    if not srt_file.is_file():
                        raise RuntimeError("Het ondertitelbestand is niet gevonden:\n"+srt_path)
                    timed=self._limit_range(read_srt(srt_file), st)
                    self.check_stop()
                    p["transcript"].write_text(format_timed_text(timed),encoding="utf-8")
                    p["segments"].write_text(json.dumps(timed,ensure_ascii=False,indent=2),encoding="utf-8")
                    self.stage(2,1,f"{len(timed)} ondertitels ingelezen.")
                    self.finish(p,st,3); done=3
                else:
                    self.stage(2,0,"Whisper-model laden...")
                    try:
                        from faster_whisper import WhisperModel
                    except ImportError as exc:
                        raise RuntimeError("Spraakherkenning ontbreekt. "+install_command_hint()) from exc
                    heard,offset=self._recognition_clip(p, recognition_source, st)
                    model=WhisperModel(st["model"],device="cpu",compute_type="int8"); segs,info=model.transcribe(str(heard),language=st["source_language"],task="transcribe",beam_size=5,vad_filter=True)
                    dur=max(float(getattr(info,"duration",0) or 0),.01); parts=[]; timed=[]
                    for seg in segs:
                        self.check_stop(); t=seg.text.strip()
                        if t:
                            parts.append(t); timed.append({"start":float(seg.start)+offset,"end":float(seg.end)+offset,"text":t})
                        self.stage(2,min(.995,float(seg.end)/dur),f"Spraak herkennen: {seg.end:.0f}/{dur:.0f} sec")
                    timed=self._limit_range(timed, st)
                    text=" ".join(spoken_text(item) for item in timed).strip()
                    if not text:raise RuntimeError("Whisper heeft geen spraak herkend.")
                    p["transcript"].write_text(format_timed_text(timed),encoding="utf-8")
                    p["segments"].write_text(json.dumps(timed,ensure_ascii=False,indent=2),encoding="utf-8"); self.finish(p,st,3); done=3
            if done<4:
                timed=json.loads(p["segments"].read_text(encoding="utf-8"))
                if st.get("edit_transcript"):
                    timed=self.wait_for_transcript_edit(timed, recognition_source)
                    p["segments"].write_text(json.dumps(timed,ensure_ascii=False,indent=2),encoding="utf-8")
                    p["transcript"].write_text(format_timed_text(timed),encoding="utf-8")
                    st["transcript_edited"]=True
                    self.save(p,st)
                note=translation_route_note(st["source_language"])
                self.stage(3,.01,"Vertaalpakket controleren. "+note if note else "Vertaalpakket controleren...")
                from telecine_setup import ensure_argos_package
                ensure_argos_package(st["source_language"])
                busy="Segmenten naar Nederlands vertalen. "+note if note else "Segmenten naar Nederlands vertalen..."
                translated=translate_segments(timed,st["source_language"],self.stop_event,lambda f:self.stage(3,f,busy))
                if not any(x.get("translation","").strip() for x in translated):raise RuntimeError("De vertaling is leeg.")
                p["translated_segments"].write_text(json.dumps(translated,ensure_ascii=False,indent=2),encoding="utf-8")
                p["translation"].write_text(format_timed_text(translated,"translation"),encoding="utf-8"); self.finish(p,st,4); done=4
            if done<5:
                if st.get("edit_translation"):
                    translated=json.loads(p["translated_segments"].read_text(encoding="utf-8"))
                    translated=self.wait_for_translation_edit(translated, recognition_source)
                    p["translated_segments"].write_text(json.dumps(translated,ensure_ascii=False,indent=2),encoding="utf-8")
                    p["translation"].write_text(format_timed_text(translated,"translation"),encoding="utf-8")
                    st["translation_edited"]=True
                    self.save(p,st)
                if not make_speech:
                    self.stage(4,1,"Vlaamse spraak overgeslagen."); self.finish(p,st,5); done=5
                else:
                    self.stage(4,.01,"Gesynchroniseerde Vlaamse segmenten genereren..."); p["voice"].unlink(missing_ok=True)
                    translated=json.loads(p["translated_segments"].read_text(encoding="utf-8"))
                    if st.get("sync_timing",True):
                        src_duration=read_wav_duration(audio)
                        warnings=build_synchronized_voice(translated,p["voice"],p["segment_audio"],st["voice"],int(st.get("max_speed",15)),src_duration,self.stop_event,lambda f:self.stage(4,f,"Vlaamse segmenten op tijdcode plaatsen..."),lambda proc:setattr(self,"child",proc))
                        st["timing_warnings"]=warnings
                    else:
                        full=" ".join(spoken_text(x,"translation") for x in translated if spoken_text(x,"translation"))
                        run_piper(full,p["voice"],st["voice"],self.stop_event,lambda proc:setattr(self,"child",proc))
                    self.queue_measure("voice",p["voice"],"voice_raw"); self.finish(p,st,5); done=5
            if done<6:
                if not make_speech:
                    self.stage(5,1,"Mix overgeslagen."); self.finish(p,st,6); done=6
                else:
                    if use_uvr:
                        self.stage(5,.1,"Vlaamse stem met originele achtergrond mengen..."); mix_wavs(p["background"],p["voice"],p["mixed"],st.get("bg_volume",100)/100,st.get("voice_volume",100)/100)
                    else: shutil.copy2(p["voice"],p["mixed"])
                    self.queue_measure("output",p["mixed"])
                    self.finish(p,st,6); done=6
            if done<7:
                self.stage(6,.2,"Resultaten opslaan..."); out=Path(st["output_dir"]); out.mkdir(parents=True,exist_ok=True); stem=safe_name(source.stem)
                shutil.copy2(p["transcript"],out/f"{stem}_transcript_{st['source_language']}.txt"); shutil.copy2(p["translation"],out/f"{stem}_vertaling_nl.txt")
                source_segments=json.loads(p["segments"].read_text(encoding="utf-8")); translated_segments=json.loads(p["translated_segments"].read_text(encoding="utf-8"))
                source_srt=out/f"{stem}_transcript_{st['source_language']}.srt"; dutch_srt=out/f"{stem}_vertaling_nl.srt"
                write_srt(source_segments,source_srt,"text"); write_srt(translated_segments,dutch_srt,"translation")
                st["subtitle_file"]=str(dutch_srt); st["output_file"]=""; st["output_video"]=""
                if make_speech:
                    final=out/f"{stem}_Vlaams_met_achtergrond.wav" if use_uvr else out/f"{stem}_Vlaams.wav"; shutil.copy2(p["mixed"],final)
                    if use_uvr and st.get("keep_stems"):
                        shutil.copy2(p["vocals"],out/f"{stem}_UVR_stem.wav"); shutil.copy2(p["background"],out/f"{stem}_UVR_achtergrond.wav")
                    st["output_file"]=str(final)
                    if is_video_file(source) and st.get("write_mp4",True):
                        self.stage(6,.65,"Vertaalde audio in de MP4 schrijven...")
                        final_mp4=out/f"{stem}_Vlaams_met_achtergrond.mp4" if use_uvr else out/f"{stem}_Vlaams.mp4"
                        write_mp4(source,final,final_mp4,self.stop_event,lambda proc:setattr(self,"child",proc))
                        st["output_video"]=str(final_mp4)
                elif is_video_file(source) and st.get("write_mp4",True):
                    self.stage(6,.65,"Nederlandse ondertitels in de film plaatsen...")
                    final_mp4=out/f"{stem}_met_ondertitels.mp4"
                    write_subtitled_mp4(source,dutch_srt,final_mp4,self.stop_event,lambda proc:setattr(self,"child",proc))
                    st["output_video"]=str(final_mp4)
                self.finish(p,st,7)
            shown=st.get("output_video") or st.get("output_file") or "Nederlandse ondertitels opgeslagen."
            if st.get("output_video") and st.get("output_file"):
                shown=st["output_video"]+"\n\nWAV-mix:\n"+st["output_file"]
            st["status"]="voltooid"; st["elapsed"]=self.elapsed_now(); self.save(p,st); self.q.put(("completed",shown,st.get("subtitle_file","")))
        except (StoppedByUser, MediaStopped):
            st["status"]="gestopt"; st["elapsed"]=self.elapsed_now(); self.save(p,st); self.q.put(("stopped",st.get("completed_steps",0)))
        except Exception as e:
            st["status"]="fout"; st["last_error"]=str(e); st["elapsed"]=self.elapsed_now(); self.save(p,st); self.q.put(("error",str(e),st.get("completed_steps",0)))
        finally:self.child=None

    def process_queue(self):
        try:
            while True:
                x=self.q.get_nowait(); kind=x[0]
                if kind=="meter":
                    _,key,values,raw_key=x
                    if raw_key: self.level_values[raw_key]=dict(values)
                    self.set_meter(key,values,reset_hold=True,remember=True)
                    if raw_key: self.preview_gain_levels()
                elif kind=="env":
                    _,gen,notes=x
                    if gen==self._env_gen:
                        self._env_notes=list(notes); self._env_ready=True; self._refresh_hint()
                elif kind=="live_levels":
                    _,levels,pos,total,sr=x
                    for key,values in levels.items(): self.set_meter(key,values)
                    if not self.play_seeking:
                        self.play_slider.set(100*pos/max(1,total)); self.play_time.set(f"{fmt(pos/sr)} / {fmt(total/sr)}")
                elif kind=="play_finished":
                    if self.play_stream or self.play_position>=self.play_total_frames:
                        self.stop_audio(reset_position=True); self.play_status.set("Afspelen voltooid")
                elif kind=="meter_error":
                    self.hint.set("Niveau niet gemeten: "+x[2])
                    if hasattr(self,"hint_label"): self.hint_label.configure(fg="#8a3b00")
                elif kind in ("edit_transcript","edit_translation"):
                    _,segments,audio,*rest=x
                    label=rest[0] if rest else ""
                    self._open_transcript_editor(segments,audio,"translation" if kind=="edit_translation" else "text",label or None)
                elif kind=="progress":
                    _,v,i,msg=x; self.set_progress(v); self.status.set(msg); self.step.set(f"Stap {i+1} van {len(STEPS)}: {STEPS[i]}"); self.show_steps(i,"running")
                elif kind=="stopped":
                    n=x[1]; self.running(False); self.resume.config(state="normal"); self.status.set("Gestopt door gebruiker"); self.resume_text.set(f"Hervatten vanaf stap {n+1}: {STEPS[min(n,len(STEPS)-1)]}"); self.show_steps(min(n,len(STEPS)-1),"paused")
                elif kind=="completed":
                    self.stop_audio()
                    self.play_status.set("Eindmix gereed voor live afspelen" if (self.state or {}).get("output_file") else "Ondertitels gereed")
                    done_note=translation_route_note((self.state or {}).get("source_language",""))
                    self.set_progress(100); self.running(False); self.resume.config(state="disabled"); self.status.set("Klaar! "+done_note if done_note else "Klaar!"); self.remaining.set("00:00:00"); srt=x[2] if len(x)>2 and x[2] else ""
                    self.show_steps(len(STEPS),"completed"); messagebox.showinfo(APP_TITLE,"Verwerking voltooid.\n\n"+x[1]+(("\n\nNederlandse ondertitels:\n"+srt) if srt else "")+(("\n\n"+done_note) if done_note else "")+("\n\nLet op: enkele segmenten waren langer dan hun tijdvak en kunnen overlappen." if self.state and self.state.get("timing_warnings") else ""))
                elif kind=="error":
                    _,err,n=x; self.running(False); self.resume.config(state="normal"); self.status.set("Fout tijdens verwerking"); self.resume_text.set(f"Hervatten vanaf stap {n+1}: {STEPS[min(n,len(STEPS)-1)]}"); self.show_steps(min(n,len(STEPS)-1),"error"); messagebox.showerror(APP_TITLE,err)
        except queue.Empty:pass
        self.after(100,self.process_queue)
    def set_progress(self,v): self.progress=max(0,min(100,float(v))); self.bar["value"]=self.progress; self.percent.set(f"{self.progress:.0f}%")
    def clock(self):
        if self.worker and self.worker.is_alive():
            e=self.elapsed_now(); self.elapsed.set(fmt(e)); pr=self.progress/100; self.remaining.set("Wordt berekend..." if pr<.03 else fmt(e*(1-pr)/pr))
        self.after(500,self.clock)
    def show_steps(self,active,mode):
        done=int(self.state.get("completed_steps",0)) if self.state else 0; lines=[]
        for i,n in enumerate(STEPS):
            mark="[OK]" if mode=="completed" or i<done else "[>>]" if i==active and mode=="running" else "[PAUZE -> HIER]" if i==active and mode=="paused" else "[FOUT -> HIER]" if i==active and mode=="error" else "[  ]"
            lines.append(f"{mark:<18} {i+1}. {n}")
        self.steps.config(state="normal"); self.steps.delete("1.0","end"); self.steps.insert("1.0","\n".join(lines)); self.steps.config(state="disabled")
    def _limit_range(self, timed, st):
        start=optional_seconds(st.get("range_start")); end=optional_seconds(st.get("range_end"))
        limited=limit_segments(timed, start, end)
        if (start is not None or end is not None) and not limited:
            raise RuntimeError("In het gekozen bereik staan geen zinnen.")
        return limited
    def _recognition_clip(self, p, source, st):
        """Herkend alleen het gekozen stuk. De tijden blijven die van de hele film."""
        start=optional_seconds(st.get("range_start")); end=optional_seconds(st.get("range_end"))
        if start is None and end is None:
            return source, 0.0
        begin=0.0 if start is None else float(start)
        clip=p["folder"]/"bereik.wav"
        extract_wav(source, clip, self.stop_event, lambda proc: setattr(self,"child",proc), None if begin<=0 else begin, end)
        return clip, begin
    def close(self):
        self._close_preview()
        self.stop_audio()
        if self.worker and self.worker.is_alive() and not messagebox.askyesno(APP_TITLE,"Er loopt nog een verwerking. Stoppen en sluiten?"):return
        if self.worker and self.worker.is_alive():self.request_stop()
        self.destroy()

if __name__=="__main__":
    try:
        JOB_DIR.mkdir(parents=True,exist_ok=True)
        App().mainloop()
    except Exception:
        import traceback
        text=traceback.format_exc()
        try: (BASE_DIR/"telecine-start.log").write_text(text,encoding="utf-8")
        except Exception: pass
        try: messagebox.showerror(APP_TITLE,"Het programma kon niet starten.\n\n"+text[-2000:])
        except Exception: pass
        raise
