# AEO-Light 3.1.21 (fork release)

Deze 3.1.21 patchrelease fixeert de videouitsnede zodat die de gevonden frame-scheiding beter volgt binnen de Search Size-zone.

## Nieuw in 3.1.21
- Bugfix in adaptieve video-crop driftberekening: de uitsnede volgt nu de gedetecteerde frame-separatielijn met correcte richting.
- Minder kans dat een deel van het beeld van het frame erboven/eronder in de output verschijnt bij drift.

## Eerder toegevoegd in 3.1.20
- Tab hernoemd naar **Extract Audio/Video**.
- Opties die voordien in een popup stonden, zitten nu rechtstreeks in deze tab:
  - audio output pad
  - video output aan/uit + video output pad
  - A/V offset-correctie (16mm, 26 frames)
  - video-oriëntatie (Spiegel H/V + rotatie + preset “Oriëntatie zoals preview”)
- Vanuit hetzelfde venster kan je nu meteen audio en/of video exporteren zonder extra popup.
- Deze output-instellingen worden onthouden (laatst gebruikte waarden en paden).

## Eerder toegevoegd in 3.1.19
- Nieuwe tab **Video instellingen** (tussen Image Processing en Extract Audio) voor altijd bereikbare video-opties.
- Compressie/formaat-instellingen in de nieuwe tab:
  - Simple export mode: **Snel / Standaard / Kwaliteit / Ultra kwaliteit**
  - container: **MP4 / MOV / MKV**
  - output-formaat: **H.264 / HEVC / MPEG-4**
  - encoder-preset, video-kwaliteit en audio-bitrate
- Laatst gebruikte video-instellingen worden onthouden tot je ze wijzigt.
- Preview window schaalt nu mee naar ongeveer dezelfde hoogte als de UI, met behoud van beeldverhouding.

## Eerder toegevoegd in 3.1.18
- Preview window wordt op gelijke hoogte naast het hoofdvenster geplaatst.
- Sub-panel titels staan nu vet (bold) in zowel light als dark theme.
- Frame Pitch Start/End fijner af te stellen:
  - verbeterde koppellogica zodat sliders elkaar minder tegenwerken
  - waarden met 3 decimalen voor preciezere feedback
  - extra tiptekst voor exacte uitlijning via pijltjestoetsen

## Eerder toegevoegd in 3.1.17
- Nieuw **Video/Audio venster...** in het extract-scherm met aparte geavanceerde instellingen:
  - container: **MP4 / MOV / MKV**
  - output-formaat: **H.264 / HEVC / MPEG-4**
  - video-kwaliteit, encoder-preset en audio-bitrate
- Nieuwe **Simple export preset: Ultra kwaliteit** (minimum artefacten, groter bestand)

## Eerder toegevoegd in 3.1.16
- Main window/layout vergroot zodat Soundtrack-secties niet meer afgekapt of overlappend staan.
- Extra visuele omkadering in Soundtrack Settings:
  - apart kader voor **Soundtrack + Picture Strip**
  - apart kader voor **Frame Pitch + Search Size**
- Hotfix: overlap opgelost in de Soundtrack Settings-groep.
- Hotfix: License Agreement terug goed leesbaar in dark theme.
- Sterkere stabiliteit bij laden/verwerken van frame-reeksen  
- Betere video-export workflow  
- Progress-bar met geschatte resterende tijd  
- “Simple export mode” met presets: **Snel / Standaard / Kwaliteit / Ultra kwaliteit**
- Veiliger stoppen: bevestigingsdialoog bij annuleren  
- Oriëntatie-opties (spiegelen/roteren) + preset **“Oriëntatie zoals preview”**  
- Optionele audio in MP4 met 16mm-correctie (26 frames), uitschakelbaar  
- Nieuwe **Test Run** export met vrij framebereik  
- Snelle Test Run-presets: **100 / 300 / 500 / 10 sec**  
- Exportbestanden worden niet overschreven (automatische oplopende suffix)  
- Nieuwe thema-switch in Preferences: **Licht / Donker** (wordt onthouden)  

## Installatie (Windows 11)
1. Download `AEO-Light-Windows-x64.zip` uit de release.
2. Pak het zip-bestand uit naar een map, bv. `C:\AEO-Light\`.
3. Start `AEO-Light.exe`.
4. Als SmartScreen waarschuwt: klik **Meer informatie** → **Toch uitvoeren**.

## Snelle start (aanbevolen)
- Container: **MP4**
- Preset: **Standaard**
- FPS: gelijk aan je bronmateriaal
- Bitrate: begin rond **8–12 Mbps** en verhoog alleen indien nodig
