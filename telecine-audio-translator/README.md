# TeleCine Audio Translator

Versie **0.9.03**. Het venster toont het nummer uit het bestand `VERSION`.

Vensterprogramma dat een Franse of Engelse WAV omzet naar Vlaamse spraak. Het behoudt de originele timing en kan de achtergrondmuziek laten staan.

Je hoeft geen IDE te openen en geen `python`-commando te typen. Na de eenmalige installatie start je het met een dubbelklik.

## Windows

1. Installeer [Python 3.12.10, 64-bit](https://www.python.org/ftp/python/3.12.10/python-3.12.10-amd64.exe) en vink **Add python.exe to PATH** en **tcl/tk** aan. Python 3.14 kan de stemscheiding niet installeren: daarvoor bestaat geen kant-en-klaar pakket, en de broncode mist `bitpack.pyx`.
2. Installeer [eSpeak NG](https://github.com/espeak-ng/espeak-ng/releases). De Vlaamse stem heeft dat nodig voor de uitspraak.
3. Pak de zip uit naar een gewone map, bijvoorbeeld het Bureaublad. Dubbelklik `installeer.bat` niet vanuit het zip-bestand.

De eerste keer heeft internet nodig. Het script maakt een virtuele omgeving, installeert de onderdelen, downloadt de stemmen Nathalie en RDH, en zet **TeleCine Audio Translator** op het bureaublad.

Engels wordt rechtstreeks naar het Nederlands vertaald. Frans gaat via Engels, omdat Argos geen rechtstreeks Frans-Nederlands pakket meer aanbiedt.

Daarna: dubbelklik dat bureaublad-icoon. Er opent geen zwart opdrachtvenster.

## Linux

Dubbelklik `start.sh`, of voer in een terminal uit:

```sh
./start.sh
```

De eerste keer installeert dat script alles en zet een item in het programmamenu. Tkinter en eSpeak NG horen op het systeem te staan:

```sh
sudo apt install python3-tk python3-venv espeak-ng
```

## macOS

In Terminal, in deze map:

```sh
./start.sh
```

Installeer eSpeak NG apart, bijvoorbeeld met Homebrew: `brew install espeak-ng`.

## Als het venster niet opent

Dubbelklik `start.bat` (Windows). Die laat de fouttekst zien. Dezelfde tekst komt in `telecine-start.log`.

Op Linux start je `./start.sh` in een terminal, zodat de melding zichtbaar blijft.

## Tijdens het gebruik

- Bron: een WAV-bestand of een MP4. Bij een MP4 blijft het beeld staan en komt de vertaling in een nieuwe MP4, als het vinkje **Bij een MP4 het beeld behouden** aanstaat. Daarvoor is [FFmpeg](https://ffmpeg.org/download.html) nodig. Zet `ffmpeg.exe` in PATH of in de programmamap. De WAV-mix wordt er ook nog bij geschreven.
- **Bronondertitels (SRT)** is optioneel. Staat daar een bestand, dan wordt de spraakherkenning overgeslagen. De tijden uit dat bestand blijven staan en worden vertaald naar het Nederlands.
- **Film bekijken** opent het beeld. Onderaan staat een schuif om frame per frame naar een moment te gaan. **Startpunt** en **Eindpunt** zetten dat frame als begin of einde. Alleen de zinnen in dat bereik worden vertaald. Het beeld van de film blijft heel. Bij een WAV is er geen beeld en kies je het bereik op de tijd.
- **Vlaamse spraak maken** staat standaard aan. Zet het uit om alleen de Nederlandse ondertitels te gebruiken. Bij een MP4 komen die dan in de film: het beeld en het oorspronkelijke geluid blijven, de ondertitel is een extra spoor. Bij een WAV blijft het resultaat het ondertitelbestand.
- Resultaat: de map `Output` (knop **UITVOERMAP OPENEN**). Daar staan, als de spraak aanstaat, de Vlaamse WAV, de tekstbestanden en de ondertitels `..._vertaling_nl.srt` en `..._transcript_<taal>.srt`. Zonder spraak komt bij een MP4 ook `..._met_ondertitels.mp4`.
- **Transcriptie bewerken voor de vertaling** staat standaard aan. Na de spraakherkenning verschijnt een venster met elke zin en haar begin- en eindtijd. Pas de zinsbouw aan en kies **Vertaling starten**.
- **Nederlandse vertaling bewerken voor de spraak** staat ook standaard aan. Na de vertaling kun je het Nederlands corrigeren. De bronzin staat erboven. Kies daarna **Spraak starten**, of **Ondertitels plaatsen** als de spraak uitstaat. De tijden blijven die van de bron, zodat de ondertitel en, als die aanstaat, de Vlaamse stem op dezelfde plaats vallen.
- Een leeg vak wordt niet uitgesproken en schuift de andere zinnen niet op. **Samenvoegen met volgende** maakt van twee opeenvolgende stukken één zin, van de eerste begintijd tot de tweede eindtijd. Zet een vinkje uit als die stap automatisch mag doorlopen.
- Bij het starten opent het venster meteen zo hoog dat de instellingen leesbaar zijn. Past dat niet op het scherm, dan gebruikt het de vrije hoogte en schuift de rest.
- **Weergave** staat rechtsboven in de instellingen. Kies **Licht** of **Donker**. Die keuze blijft bij de volgende start.
- De niveaumeters, het vinkje voor stemscheiding, de afspeelknoppen, **AUDIO VERTALEN**, **STOP**, **HERVATTEN**, **UITVOERMAP OPENEN** en het voortgangsvak blijven in beeld. Volumes en synchronisatie schuiven mee als het venster te laag is.
- Ontbreekt een stem, eSpeak NG, stemscheiding of een vertaalpakket, dan staat dat in het vak boven de knoppen.
- Stemscheiding (UVR) is standaard aan en is een grote extra download. Lukt die niet, vink **Stem met UVR scheiden** uit. Het programma start dan nog steeds.
- Een gestopte vertaling kun je hervatten met **HERVATTEN**. De taal, de stem en de volumes van die taak komen terug.

## Starten bij aanmelden

Standaard start het programma niet vanzelf als de computer aangaat. Het is een venster dat je zelf opent.

Wil je het wel bij elke aanmelding:

- Windows: dubbelklik `start-bij-aanmelden.bat`. Haal de snelkoppeling weg uit de map Opstarten om het uit te zetten.
- Linux: `./start-bij-aanmelden.sh`. Verwijder `~/.config/autostart/telecine-audio-translator.desktop` om het uit te zetten.

## Versie en releases

Het nummer staat in `VERSION`. Het venster en de GitHub-release gebruiken datzelfde nummer. Een tag verhoogt zichzelf niet.

De eerste release is `v0.9.01`. Een volgende release:

1. Draai `python verhoog_versie.py`. Dat maakt van `0.9.01` bijvoorbeeld `0.9.02`, in `VERSION` en in de eerste regel hierboven.
2. Publiceer tag `v` plus de inhoud van `VERSION`, bijvoorbeeld `v0.9.02`, op `main`.

De release staat daarna op [Releases](https://github.com/stoefer/TeleCineAudioTranslator/releases). Een tag die met `v` begint publiceert de release ook via GitHub Actions.

## Onderdelen

| Bestand | Rol |
| --- | --- |
| `VERSION` | Huidige versie, ook zichtbaar in het venster |
| `verhoog_versie.py` | Verhoogt het laatste deel van `VERSION` voor de volgende release |
| `installeer.bat` / `installeer.sh` | Eenmalige installatie |
| `start.vbs` | Windows-start zonder opdrachtvenster |
| `start.sh` | Linux/macOS-start, installeert de eerste keer zelf |
| `requirements.txt` | Spraakherkenning, vertaling, stem en afspelen |
| `requirements-uvr.txt` | Optionele stemscheiding |
| `voices/` | Vlaamse Piper-stemmen, automatisch gedownload |
