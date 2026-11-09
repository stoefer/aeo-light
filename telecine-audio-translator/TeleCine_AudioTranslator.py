# -*- coding: utf-8 -*-
"""TeleCine Audio Translator met optionele UVR-stemscheiding.

Starten zonder terminal:
  Windows: dubbelklik installeer.bat (eenmalig), daarna het bureaublad-icoon.
  Linux:   ./installeer.sh (eenmalig), daarna ./start.sh

Vereist: faster-whisper, argostranslate, piper-tts, sounddevice, soundfile, numpy.
Voor UVR: python -m pip install "audio-separator[cpu]"
"""
import hashlib, json, os, queue, re, shutil, subprocess, sys, threading, time, wave
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

APP_TITLE = "TeleCine Audio Translator"
BASE_DIR = Path(__file__).resolve().parent
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
WHISPER_MODELS = ["tiny", "base", "small", "medium"]
STEPS = ["Bestand controleren", "Franse stem scheiden (UVR)", "Spraak herkennen",
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
        for mark in (-24,-12,-6,-3):
            xm=self._x(mark,w); canvas.create_line(xm,0,xm,h,fill="#555")
        if hold is not None and hold>-120:
            xh=self._x(hold,w); canvas.create_line(xh,0,xh,h,fill="white",width=2)
    def set_levels(self,peak,rms,reset_hold=False):
        self.values=(float(peak),float(rms))
        p=dbfs(peak); r=dbfs(rms)
        if reset_hold: self.peak_hold=p
        else: self.peak_hold=max(self.peak_hold,p)
        self.peak_text.config(text=f"Peak {p:6.1f} dBFS"); self.rms_text.config(text=f"RMS  {r:6.1f} dBFS")
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
    subprocess.Popen([opener, target], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def read_wav_duration(path):
    with wave.open(str(path), "rb") as w: return w.getnframes() / max(1, w.getframerate())

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

def translate_segments(segments, source, stop, callback):
    import argostranslate.translate as tr
    out=[]; total=max(1,len(segments))
    for i, seg in enumerate(segments,1):
        if stop.is_set(): raise StoppedByUser
        item=dict(seg); item["translation"]=tr.translate(seg["text"],source,"nl").strip(); out.append(item)
        callback(i/total)
    return out

def run_piper(text, output, voice_name, stop, child_setter):
    env=os.environ.copy(); es=find_espeak_data()
    if es: env["ESPEAK_DATA_PATH"]=es
    cmd=[sys.executable,"-m","piper","--data-dir",str(VOICE_DIR),"-m",voice_name,"-f",str(output)]
    flags=subprocess.CREATE_NO_WINDOW if os.name=="nt" else 0
    proc=subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,
                          encoding="utf-8",errors="replace",env=env,creationflags=flags)
    child_setter(proc)
    try:
        _,err=proc.communicate(input=text)
    finally:
        child_setter(None)
    if stop.is_set(): raise StoppedByUser
    if proc.returncode!=0 or not Path(output).exists(): raise RuntimeError("Piper-fout:\n"+err)

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
        wav=temp_dir/f"segment_{i:05d}.wav"
        run_piper(seg.get("translation","") or " ",wav,voice_name,stop,child_setter)
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
    if bg.shape[1] == 1 and channels == 2: bg=np.repeat(bg,2,axis=1)
    if vc.shape[1] == 1 and channels == 2: vc=np.repeat(vc,2,axis=1)
    if bg.shape[1] > channels: bg=bg[:,:channels]
    if vc.shape[1] > channels: vc=vc[:,:channels]
    length=max(len(bg),len(vc)); mixed=np.zeros((length,channels),dtype="float32")
    mixed[:len(bg)] += bg*bg_gain; mixed[:len(vc)] += vc*voice_gain
    peak=float(np.max(np.abs(mixed))) if mixed.size else 0
    if peak>0.98: mixed *= 0.98/peak
    sf.write(str(output), mixed, sr_bg, subtype="PCM_16")

class App(tk.Tk):
    def __init__(self):
        super().__init__(); self.title(APP_TITLE); self.geometry("1000x900"); self.minsize(860,640)
        self.q=queue.Queue(); self.stop_event=threading.Event(); self.worker=None; self.child=None
        self.started=None; self.elapsed_before=0.; self.progress=0.; self.state=None
        self.source=tk.StringVar(); self.outdir=tk.StringVar(value=str(BASE_DIR/"Output"))
        self.language=tk.StringVar(value="Frans"); self.whisper=tk.StringVar(value="small")
        self.voice_choice=tk.StringVar(value=DEFAULT_VOICE_LABEL)
        self.use_uvr=tk.BooleanVar(value=True); self.keep_stems=tk.BooleanVar(value=True)
        self.sync_timing=tk.BooleanVar(value=True); self.max_speed=tk.IntVar(value=15)
        self.bg_volume=tk.IntVar(value=100); self.voice_volume=tk.IntVar(value=100)
        self.level_values={}; self.meters={}
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
        self.build(); self.after(100,self.process_queue); self.after(500,self.clock); self.protocol("WM_DELETE_WINDOW",self.close)

    def build(self):
        outer=ttk.Frame(self); outer.pack(fill="both",expand=True)
        outer.rowconfigure(0,weight=1); outer.columnconfigure(0,weight=1)
        canvas=tk.Canvas(outer,highlightthickness=0,borderwidth=0)
        scroll=ttk.Scrollbar(outer,orient="vertical",command=canvas.yview)
        canvas.configure(yscrollcommand=scroll.set)
        canvas.grid(row=0,column=0,sticky="nsew"); scroll.grid(row=0,column=1,sticky="ns")
        bottom=ttk.Frame(outer,padding=(16,0,16,12)); bottom.grid(row=1,column=0,columnspan=2,sticky="ew")
        bottom.columnconfigure(0,weight=1); bottom.rowconfigure(1,weight=1)
        m=ttk.Frame(canvas,padding=16); m.columnconfigure(1,weight=1)
        window=canvas.create_window((0,0),window=m,anchor="nw")
        def fit(_event=None):
            canvas.configure(scrollregion=canvas.bbox("all"))
            canvas.itemconfigure(window,width=max(1,canvas.winfo_width()))
        m.bind("<Configure>",fit); canvas.bind("<Configure>",fit)
        canvas.bind_all("<MouseWheel>",lambda e: canvas.yview_scroll(int(-e.delta/120) or (-1 if e.delta>0 else 1),"units"))
        canvas.bind_all("<Button-4>",lambda e: canvas.yview_scroll(-3,"units"))
        canvas.bind_all("<Button-5>",lambda e: canvas.yview_scroll(3,"units"))
        ttk.Label(m,text=APP_TITLE,font=("Segoe UI",18,"bold")).grid(row=0,column=0,columnspan=3,sticky="w",pady=(0,14))
        ttk.Label(m,text="Bronbestand (WAV):").grid(row=1,column=0,sticky="w"); ttk.Entry(m,textvariable=self.source).grid(row=1,column=1,sticky="ew",padx=8)
        ttk.Button(m,text="Kiezen...",command=self.choose_source).grid(row=1,column=2)
        ttk.Label(m,text="Uitvoermap:").grid(row=2,column=0,sticky="w",pady=6); ttk.Entry(m,textvariable=self.outdir).grid(row=2,column=1,sticky="ew",padx=8)
        ttk.Button(m,text="Kiezen...",command=self.choose_out).grid(row=2,column=2)
        opt=ttk.LabelFrame(m,text="Vertaling",padding=10); opt.grid(row=3,column=0,columnspan=3,sticky="ew",pady=8)
        for c in range(3): opt.columnconfigure(c,weight=1)
        ttk.Label(opt,text="Brontaal").grid(row=0,column=0,sticky="w"); ttk.Label(opt,text="Whisper-kwaliteit").grid(row=0,column=1,sticky="w"); ttk.Label(opt,text="Vlaamse stem").grid(row=0,column=2,sticky="w")
        self.lang=ttk.Combobox(opt,textvariable=self.language,values=list(LANGUAGES),state="readonly"); self.lang.grid(row=1,column=0,sticky="ew")
        self.model=ttk.Combobox(opt,textvariable=self.whisper,values=WHISPER_MODELS,state="readonly"); self.model.grid(row=1,column=1,sticky="ew",padx=8)
        self.voice=ttk.Combobox(opt,textvariable=self.voice_choice,values=list(VOICES),state="readonly"); self.voice.current(0); self.voice.grid(row=1,column=2,sticky="ew")
        u=ttk.LabelFrame(m,text="UVR: Franse/Engelse stem verwijderen en achtergrond behouden",padding=10); u.grid(row=4,column=0,columnspan=3,sticky="ew",pady=8)
        u.columnconfigure(1,weight=1)
        self.uvr_check=ttk.Checkbutton(u,text="Stem met UVR scheiden en originele achtergrond/muziek behouden",variable=self.use_uvr); self.uvr_check.grid(row=0,column=0,columnspan=3,sticky="w")
        ttk.Checkbutton(u,text="Losse UVR-sporen ook naar de uitvoermap kopiëren",variable=self.keep_stems).grid(row=1,column=0,columnspan=3,sticky="w",pady=(3,8))
        ttk.Label(u,text="Achtergrondvolume:").grid(row=2,column=0,sticky="w"); ttk.Scale(u,from_=0,to=150,variable=self.bg_volume,orient="horizontal",command=lambda v:self.preview_gain_levels()).grid(row=2,column=1,sticky="ew",padx=8); ttk.Label(u,textvariable=self.bg_volume,width=4).grid(row=2,column=2)
        ttk.Label(u,text="Vlaamse stemvolume:").grid(row=3,column=0,sticky="w"); ttk.Scale(u,from_=0,to=150,variable=self.voice_volume,orient="horizontal",command=lambda v:self.preview_gain_levels()).grid(row=3,column=1,sticky="ew",padx=8); ttk.Label(u,textvariable=self.voice_volume,width=4).grid(row=3,column=2)
        sy=ttk.LabelFrame(m,text="Synchronisatie met originele timing",padding=10); sy.grid(row=5,column=0,columnspan=3,sticky="ew",pady=8)
        sy.columnconfigure(1,weight=1)
        self.sync_check=ttk.Checkbutton(sy,text="Oorspronkelijke starttijden en stiltes behouden",variable=self.sync_timing); self.sync_check.grid(row=0,column=0,columnspan=3,sticky="w")
        ttk.Label(sy,text="Maximale versnelling van Vlaamse segmenten:").grid(row=1,column=0,sticky="w",pady=(6,0))
        ttk.Combobox(sy,textvariable=self.max_speed,values=[0,5,10,15,20,25,30],state="readonly",width=8).grid(row=1,column=1,sticky="w",padx=8,pady=(6,0))
        ttk.Label(sy,text="%").grid(row=1,column=2,sticky="w",pady=(6,0))
        meterbox=ttk.LabelFrame(m,text="Audioniveaus: Peak, RMS en Peak Hold",padding=10); meterbox.grid(row=6,column=0,columnspan=3,sticky="ew",pady=8)
        meterbox.columnconfigure(0,weight=1)
        for row,(key,label) in enumerate((("input","IN origineel"),("background","UVR-achtergrond"),("voice","Vlaamse stem"),("output","OUT eindmix"))):
            meter=LevelMeter(meterbox,label); meter.grid(row=row,column=0,sticky="ew",pady=2); self.meters[key]=meter
        ttk.Label(meterbox,text="Groen: normaal   Geel: hoog   Rood: dicht bij 0 dBFS   Witte lijn: Peak Hold",foreground="#555").grid(row=4,column=0,sticky="w",pady=(6,0))
        player=ttk.LabelFrame(meterbox,text="Resultaat afspelen met live meters",padding=8); player.grid(row=5,column=0,sticky="ew",pady=(10,0)); player.columnconfigure(3,weight=1)
        self.play_button=ttk.Button(player,text="▶ Afspelen",command=self.play_audio); self.play_button.grid(row=0,column=0,padx=(0,5))
        self.pause_button=ttk.Button(player,text="⏸ Pauze",command=self.pause_audio,state="disabled"); self.pause_button.grid(row=0,column=1,padx=5)
        self.play_stop_button=ttk.Button(player,text="⏹ Stop",command=self.stop_audio,state="disabled"); self.play_stop_button.grid(row=0,column=2,padx=5)
        self.timeline=ttk.Scale(player,from_=0,to=100,variable=self.play_slider,orient="horizontal",command=self.preview_seek); self.timeline.grid(row=0,column=3,sticky="ew",padx=8)
        self.timeline.bind("<ButtonPress-1>",self.seek_begin); self.timeline.bind("<ButtonRelease-1>",self.seek_end)
        ttk.Label(player,textvariable=self.play_time,width=21).grid(row=0,column=4,padx=(5,0))
        ttk.Label(player,textvariable=self.play_status).grid(row=1,column=0,columnspan=5,sticky="w",pady=(5,0))
        b=ttk.Frame(bottom); b.grid(row=0,column=0,pady=(4,8))
        self.start=ttk.Button(b,text="AUDIO VERTALEN",command=self.start_new); self.start.pack(side="left",padx=5)
        self.stop=ttk.Button(b,text="STOP",command=self.request_stop,state="disabled"); self.stop.pack(side="left",padx=5)
        self.resume=ttk.Button(b,text="HERVATTEN",command=self.resume_job,state="disabled"); self.resume.pack(side="left",padx=5)
        ttk.Button(b,text="UITVOERMAP OPENEN",command=self.open_out).pack(side="left",padx=5)
        p=ttk.LabelFrame(bottom,text="Voortgang",padding=12); p.grid(row=1,column=0,sticky="nsew"); p.columnconfigure(0,weight=1); p.rowconfigure(7,weight=1)
        top=ttk.Frame(p); top.grid(row=0,column=0,sticky="ew"); top.columnconfigure(0,weight=1)
        self.bar=ttk.Progressbar(top,maximum=100); self.bar.grid(row=0,column=0,sticky="ew"); ttk.Label(top,textvariable=self.percent,width=7).grid(row=0,column=1)
        ttk.Label(p,textvariable=self.status,font=("Segoe UI",11,"bold")).grid(row=1,column=0,sticky="w",pady=(10,2)); ttk.Label(p,textvariable=self.step).grid(row=2,column=0,sticky="w"); ttk.Label(p,textvariable=self.resume_text).grid(row=3,column=0,sticky="w")
        t=ttk.Frame(p); t.grid(row=4,column=0,sticky="w",pady=8); ttk.Label(t,text="Verstreken:").grid(row=0,column=0); ttk.Label(t,textvariable=self.elapsed).grid(row=0,column=1,padx=8); ttk.Label(t,text="Resterend:").grid(row=1,column=0); ttk.Label(t,textvariable=self.remaining).grid(row=1,column=1,padx=8)
        self.steps=tk.Text(p,height=7,state="disabled",font=("Consolas",10)); self.steps.grid(row=7,column=0,sticky="nsew"); self.show_steps(0,None)

    def set_meter(self,key,values,reset_hold=False):
        self.level_values[key]=dict(values)
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
        tracks={"input":Path(source),"voice":p["voice"],"output":final}
        if st.get("use_uvr",self.use_uvr.get()): tracks["background"]=p["background"]
        return {k:v for k,v in tracks.items() if v and Path(v).is_file()}

    def _close_play_files(self):
        for f in list(self.play_files.values()):
            try:f.close()
            except:pass
        self.play_files={}

    def _reset_live_meters(self):
        for key in ("input","background","voice","output"):
            self.set_meter(key,{"peak":0.0,"rms":0.0},reset_hold=True)

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
        if reset_position:self.play_position=0; self.play_slider.set(0); self.play_time.set("00:00:00 / "+fmt(self.play_total_frames/self.play_samplerate if self.play_samplerate else 0)); self._reset_live_meters()
        self.pause_button.config(state="disabled",text="⏸ Pauze"); self.play_stop_button.config(state="disabled")
        self.play_status.set("Afspelen gestopt")

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
        f=filedialog.askopenfilename(title="Kies WAV",filetypes=[("WAV-audio","*.wav"),("Alle bestanden","*.*")])
        if f: self.source.set(f); self.inspect_resume()
    def choose_out(self):
        f=filedialog.askdirectory(title="Kies uitvoermap")
        if f: self.outdir.set(f)
    def open_out(self):
        try:
            open_folder(self.outdir.get() or BASE_DIR/"Output")
        except Exception as e:
            messagebox.showerror(APP_TITLE, "De uitvoermap kon niet geopend worden:\n"+str(e))
    def paths(self, source):
        s=Path(source).resolve(); sig=f"{s}|{s.stat().st_mtime_ns}|{s.stat().st_size}"; jid=hashlib.sha256(sig.encode()).hexdigest()[:16]; d=JOB_DIR/f"{safe_name(s.stem)}_{jid}"
        return {"folder":d,"state":d/"state.json","transcript":d/"transcript.txt","translation":d/"translation_nl.txt","vocals":d/"uvr_vocals.wav","background":d/"uvr_background.wav","voice":d/"vlaamse_stem.wav","mixed":d/"vlaams_met_achtergrond.wav","uvr_out":d/"uvr_output","segments":d/"segments.json","translated_segments":d/"segments_nl.json","segment_audio":d/"segment_audio"}
    def default_state(self,s):
        return {"source":str(Path(s).resolve()),"source_language":LANGUAGES[self.language.get()],"model":self.whisper.get(),"voice":VOICES[self.voice_choice.get()],"voice_label":self.voice_choice.get(),"use_uvr":bool(self.use_uvr.get()),"keep_stems":bool(self.keep_stems.get()),"bg_volume":int(self.bg_volume.get()),"voice_volume":int(self.voice_volume.get()),"sync_timing":bool(self.sync_timing.get()),"max_speed":int(self.max_speed.get()),"completed_steps":0,"elapsed":0.,"status":"nieuw","output_dir":str(Path(self.outdir.get() or BASE_DIR/"Output").resolve()),"output_file":"","last_error":""}
    def load(self,p):
        try:return json.loads(p["state"].read_text(encoding="utf-8"))
        except:return None
    def save(self,p,s):
        p["folder"].mkdir(parents=True,exist_ok=True); tmp=p["state"].with_suffix(".tmp"); tmp.write_text(json.dumps(s,ensure_ascii=False,indent=2),encoding="utf-8"); tmp.replace(p["state"])
    def validate(self):
        s=Path(self.source.get().strip())
        if not s.is_file(): messagebox.showerror(APP_TITLE,"Kies eerst een geldig WAV-bestand."); return None
        voice_name=VOICES.get(self.voice_choice.get())
        if not voice_name:
            messagebox.showerror(APP_TITLE,"Kies eerst een geldige Vlaamse stem."); return None
        missing=[]
        if not (VOICE_DIR/f"{voice_name}.onnx").exists(): missing.append(f"{voice_name}.onnx")
        if not (VOICE_DIR/f"{voice_name}.onnx.json").exists(): missing.append(f"{voice_name}.onnx.json")
        if missing:
            messagebox.showerror(APP_TITLE,"De gekozen Piper-stem ontbreekt in de map voices:\n\n"+"\n".join(missing)); return None
        if self.use_uvr.get() and not self.separator_exe():
            messagebox.showerror(APP_TITLE,'UVR-onderdeel ontbreekt. Voer in de actieve venv uit:\n\npython -m pip install "audio-separator[cpu]"'); return None
        return s
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
        if not s or not Path(s).is_file():return
        st=self.load(self.paths(s))
        if st and 0<st.get("completed_steps",0)<len(STEPS):
            n=st["completed_steps"]; self.state=st; self.resume.config(state="normal"); self.resume_text.set(f"Hervatten vanaf stap {n+1}: {STEPS[n]}"); self.set_progress(sum(WEIGHTS[:n])*100); self.show_steps(n,"paused")
    def resume_job(self):
        s=self.validate()
        if not s:return
        p=self.paths(s); st=self.load(p)
        if not st or st.get("completed_steps",0)>=len(STEPS): messagebox.showinfo(APP_TITLE,"Geen gestopte taak gevonden."); return
        self.voice_choice.set(st.get("voice_label", next((label for label,name in VOICES.items() if name==st.get("voice")), DEFAULT_VOICE_LABEL))); self.use_uvr.set(st.get("use_uvr",False)); self.bg_volume.set(st.get("bg_volume",100)); self.voice_volume.set(st.get("voice_volume",100)); self.sync_timing.set(st.get("sync_timing",True)); self.max_speed.set(st.get("max_speed",15)); self.launch(p,st)
    def launch(self,p,st):
        if self.worker and self.worker.is_alive():return
        self.stop_event.clear(); self.started=time.monotonic(); self.elapsed_before=float(st.get("elapsed",0)); self.state=st; self.progress=sum(WEIGHTS[:st.get("completed_steps",0)])*100; self.running(True)
        self.worker=threading.Thread(target=self.pipeline,args=(p,st),daemon=True); self.worker.start()
    def running(self,on):
        self.start.config(state="disabled" if on else "normal"); self.stop.config(state="normal" if on else "disabled"); self.resume.config(state="disabled" if on else self.resume.cget("state"));
        for x in (self.lang,self.model,self.voice,self.uvr_check,self.sync_check): x.config(state="disabled" if on else ("readonly" if isinstance(x,ttk.Combobox) else "normal"))
    def request_stop(self):
        self.stop_event.set(); self.status.set("Stop aangevraagd..."); self.stop.config(state="disabled")
        if self.child and self.child.poll() is None:
            try:self.child.terminate()
            except:pass
    def check_stop(self):
        if self.stop_event.is_set():raise StoppedByUser
    def stage(self,i,f,msg):
        f=max(0,min(1,float(f))); val=(sum(WEIGHTS[:i])+WEIGHTS[i]*f)*100; self.progress=val; self.q.put(("progress",val,i,msg))
    def finish(self,p,st,n):
        self.check_stop(); st["completed_steps"]=n; st["elapsed"]=self.elapsed_now(); st["status"]="bezig"; self.save(p,st); self.stage(n-1,1,f"Voltooid: {STEPS[n-1]}")
    def elapsed_now(self):return self.elapsed_before+(time.monotonic()-self.started if self.started else 0)

    def pipeline(self,p,st):
        try:
            p["folder"].mkdir(parents=True,exist_ok=True); source=Path(st["source"]); done=int(st.get("completed_steps",0))
            if done<1:
                self.stage(0,.2,"Bestand en installatie controleren..."); self.check_stop()
                if not source.exists():raise FileNotFoundError(source)
                self.queue_measure("input",source)
                self.finish(p,st,1); done=1
            if done<2:
                if not st.get("use_uvr",False):
                    self.stage(1,1,"UVR overgeslagen."); self.finish(p,st,2); done=2
                else:
                    self.stage(1,.02,"UVR-model laden en stem van achtergrond scheiden..."); p["uvr_out"].mkdir(exist_ok=True); MODEL_DIR.mkdir(exist_ok=True)
                    for f in p["uvr_out"].glob("*"): f.unlink()
                    cmd=[self.separator_exe(),str(source),"--model_filename",UVR_MODEL,"--output_format","WAV","--output_dir",str(p["uvr_out"]),"--model_file_dir",str(MODEL_DIR)]
                    self.child=subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding="utf-8",errors="replace",creationflags=subprocess.CREATE_NO_WINDOW if os.name=="nt" else 0)
                    lines=[]
                    while self.child.poll() is None:
                        self.check_stop(); line=self.child.stdout.readline()
                        if line: lines.append(line); self.stage(1,.35,"UVR is bezig; dit kan op CPU lang duren...")
                        else: time.sleep(.2)
                    rest=self.child.stdout.read() if self.child.stdout else ""; lines.append(rest); rc=self.child.returncode; self.child=None
                    if rc!=0: raise RuntimeError("UVR kon de audio niet scheiden.\n"+"".join(lines)[-2500:])
                    files=list(p["uvr_out"].glob("*.wav")); vocals=next((f for f in files if "vocal" in f.name.lower()),None); instr=next((f for f in files if any(x in f.name.lower() for x in ("instrumental","no_vocal","karaoke"))),None)
                    if not vocals or not instr: raise RuntimeError("UVR-uitvoer bevat niet zowel Vocals als Instrumental: "+", ".join(f.name for f in files))
                    shutil.copy2(vocals,p["vocals"]); shutil.copy2(instr,p["background"]); self.queue_measure("background",p["background"],"background_raw"); self.stage(1,1,"UVR-scheiding voltooid."); self.finish(p,st,2); done=2
            recognition_source=p["vocals"] if st.get("use_uvr") else source
            if done<3:
                self.stage(2,0,"Whisper-model laden..."); from faster_whisper import WhisperModel
                model=WhisperModel(st["model"],device="cpu",compute_type="int8"); segs,info=model.transcribe(str(recognition_source),language=st["source_language"],task="transcribe",beam_size=5,vad_filter=True)
                dur=max(float(getattr(info,"duration",0) or 0),.01); parts=[]; timed=[]
                for seg in segs:
                    self.check_stop(); t=seg.text.strip()
                    if t:
                        parts.append(t); timed.append({"start":float(seg.start),"end":float(seg.end),"text":t})
                    self.stage(2,min(.995,float(seg.end)/dur),f"Spraak herkennen: {seg.end:.0f}/{dur:.0f} sec")
                text=" ".join(parts).strip()
                if not text:raise RuntimeError("Whisper heeft geen spraak herkend.")
                p["transcript"].write_text("\n".join(f"[{hhmmss_ms(x['start'])} - {hhmmss_ms(x['end'])}] {x['text']}" for x in timed),encoding="utf-8")
                p["segments"].write_text(json.dumps(timed,ensure_ascii=False,indent=2),encoding="utf-8"); self.finish(p,st,3); done=3
            if done<4:
                self.stage(3,.01,"Vertaalpakket controleren...")
                from telecine_setup import ensure_argos_package
                ensure_argos_package(st["source_language"])
                timed=json.loads(p["segments"].read_text(encoding="utf-8"))
                translated=translate_segments(timed,st["source_language"],self.stop_event,lambda f:self.stage(3,f,"Segmenten naar Nederlands vertalen..."))
                if not any(x.get("translation","").strip() for x in translated):raise RuntimeError("De vertaling is leeg.")
                p["translated_segments"].write_text(json.dumps(translated,ensure_ascii=False,indent=2),encoding="utf-8")
                p["translation"].write_text("\n".join(f"[{hhmmss_ms(x['start'])} - {hhmmss_ms(x['end'])}] {x['translation']}" for x in translated),encoding="utf-8"); self.finish(p,st,4); done=4
            if done<5:
                self.stage(4,.01,"Gesynchroniseerde Vlaamse segmenten genereren..."); p["voice"].unlink(missing_ok=True)
                translated=json.loads(p["translated_segments"].read_text(encoding="utf-8"))
                if st.get("sync_timing",True):
                    src_duration=read_wav_duration(source)
                    warnings=build_synchronized_voice(translated,p["voice"],p["segment_audio"],st["voice"],int(st.get("max_speed",15)),src_duration,self.stop_event,lambda f:self.stage(4,f,"Vlaamse segmenten op tijdcode plaatsen..."),lambda proc:setattr(self,"child",proc))
                    st["timing_warnings"]=warnings
                else:
                    full=" ".join(x.get("translation","") for x in translated)
                    run_piper(full,p["voice"],st["voice"],self.stop_event,lambda proc:setattr(self,"child",proc))
                self.queue_measure("voice",p["voice"],"voice_raw"); self.finish(p,st,5); done=5
            if done<6:
                if st.get("use_uvr"):
                    self.stage(5,.1,"Vlaamse stem met originele achtergrond mengen..."); mix_wavs(p["background"],p["voice"],p["mixed"],st.get("bg_volume",100)/100,st.get("voice_volume",100)/100)
                else: shutil.copy2(p["voice"],p["mixed"])
                self.queue_measure("output",p["mixed"])
                self.finish(p,st,6); done=6
            if done<7:
                self.stage(6,.2,"Resultaten opslaan..."); out=Path(st["output_dir"]); out.mkdir(parents=True,exist_ok=True); stem=safe_name(source.stem)
                final=out/f"{stem}_Vlaams_met_achtergrond.wav" if st.get("use_uvr") else out/f"{stem}_Vlaams.wav"; shutil.copy2(p["mixed"],final)
                shutil.copy2(p["transcript"],out/f"{stem}_transcript_{st['source_language']}.txt"); shutil.copy2(p["translation"],out/f"{stem}_vertaling_nl.txt")
                if st.get("use_uvr") and st.get("keep_stems"):
                    shutil.copy2(p["vocals"],out/f"{stem}_UVR_stem.wav"); shutil.copy2(p["background"],out/f"{stem}_UVR_achtergrond.wav")
                st["output_file"]=str(final); self.finish(p,st,7)
            st["status"]="voltooid"; st["elapsed"]=self.elapsed_now(); self.save(p,st); self.q.put(("completed",st["output_file"]))
        except StoppedByUser:
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
                    self.set_meter(key,values,reset_hold=True)
                    if raw_key: self.preview_gain_levels()
                elif kind=="live_levels":
                    _,levels,pos,total,sr=x
                    for key,values in levels.items(): self.set_meter(key,values)
                    if not self.play_seeking:
                        self.play_slider.set(100*pos/max(1,total)); self.play_time.set(f"{fmt(pos/sr)} / {fmt(total/sr)}")
                elif kind=="play_finished":
                    if self.play_stream or self.play_position>=self.play_total_frames:
                        self.stop_audio(reset_position=True); self.play_status.set("Afspelen voltooid")
                elif kind=="meter_error":
                    pass
                elif kind=="progress":
                    _,v,i,msg=x; self.set_progress(v); self.status.set(msg); self.step.set(f"Stap {i+1} van {len(STEPS)}: {STEPS[i]}"); self.show_steps(i,"running")
                elif kind=="stopped":
                    n=x[1]; self.running(False); self.resume.config(state="normal"); self.status.set("Gestopt door gebruiker"); self.resume_text.set(f"Hervatten vanaf stap {n+1}: {STEPS[min(n,len(STEPS)-1)]}"); self.show_steps(min(n,len(STEPS)-1),"paused")
                elif kind=="completed":
                    self.stop_audio(); self.play_status.set("Eindmix gereed voor live afspelen")
                    self.set_progress(100); self.running(False); self.resume.config(state="disabled"); self.status.set("Klaar!"); self.remaining.set("00:00:00"); self.show_steps(len(STEPS),"completed"); messagebox.showinfo(APP_TITLE,"Verwerking voltooid.\n\n"+x[1]+("\n\nLet op: enkele segmenten waren langer dan hun tijdvak en kunnen overlappen." if self.state and self.state.get("timing_warnings") else ""))
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
    def close(self):
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
