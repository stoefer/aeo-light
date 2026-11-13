# AEO-Light 3.1.52 (fork release)

Deze 3.1.52 patchrelease bevat een extra hardening van de bronherkenning met gedetailleerdere foutdiagnose bij mislukte source-load.

## Nieuw in 3.1.52
- Source sequence-detectie herschreven met Qt-bestandslisting voor robuustere verwerking van scanner-JPG reeksen.
- Verbeterde UTF-8 padafhandeling voor JPG/PNG source-open.
- Veiliger opbouw van interne source-pattern strings (`name/path/fnbuf`), inclusief escape van `%` in bestandsnamen.
- Uitgebreide parserdiagnose bij laadfouten: popup toont nu onderliggende redenen per bronparser (DPX/TIFF/JPG/PNG/WAV/Video).

## Eerder toegevoegd in 3.1.51
- Betere bron-auto-detectie: als gekozen filefilter niet exact matcht, probeert AEO-Light nu automatisch alternatieve bronparsers i.p.v. direct te falen.
- MP4 fallback in builds zonder ingebouwde libav: video wordt via `ffmpeg` naar tijdelijke PNG-frames gedecodeerd en daarna geladen.
- Extra robuustheid voor pad/encoding bij bronopenen (UTF-8 overdracht).
- Tijdelijke frame-mappen uit video-fallback worden automatisch opgeruimd.

## Eerder toegevoegd in 3.1.50
- Fix: JPG-bronnen met niet-strikte sequence-naam worden nu veilig geaccepteerd als single-frame bron in plaats van geweigerd.
- Fix: sequence-scan voor beeldreeksen vergelijkt extensies nu case-insensitive (`.jpg`/`.JPG` mix werkt).
- Fix: wanneer geen geldige sequence kan worden afgeleid, valt AEO-Light automatisch terug op een single-frame source.

## Eerder toegevoegd in 3.1.49
- Fix: JPG/PNG import herkent nu het echte beeldformaat op basis van bestandsinhoud i.p.v. enkel extensie.
- Hierdoor worden scannerbestanden met afwijkende/extensie-onzekere naming voortaan correct geaccepteerd als de inhoud geldig is.
- Duidelijkere foutmelding wanneer een bestand wel `.jpg`/`.png` heet maar inhoudelijk geen geldig JPEG/PNG is.

## Eerder toegevoegd in 3.1.48
- Layout-tuning voor tab/paneelverhoudingen op kleinere en grotere schermen (HD/FHD/4K).
- Handmatige override in Preferences:
  - **UI-layout resolutieprofiel** = **Auto / HD / FHD / 4K-UHD**.
- Nieuwe statusbalk-badge met actief profiel:
  - **Layout: Auto->FHD** of **Layout: 4K (forced)**.
- Nieuwe mini-optie in Preferences om de layoutprofiel-badge te tonen/verbergen.
- Tabs beter leesbaar gemaakt in het sub-paneel:
  - full-width tabverdeling
  - kortere labels zonder afkapping waar mogelijk.

## Eerder toegevoegd in 3.1.47
- **Project relink-flow** bij ontbrekende bron:
  - kies direct een vervangend bestand, of selecteer een map om automatisch te zoeken (incl. submappen)
  - herlink gebeurt veilig, met tip om project opnieuw op te slaan
- **Autosave + herstel na onverwachte afsluiting**:
  - periodieke snapshot van sessie-instellingen
  - bij onclean vorige sessie verschijnt prompt om de vorige sessie te herstellen
- **Factory Reset** toegevoegd naast **Reset Section** voor:
  - Soundtrack / Frame Pitch / Video-crop
  - Image Processing
  - Extract / Audio / Output
- Nieuwe File-menu acties:
  - **Project Health Check** (bron/output/ffmpeg/schijfruimte)
  - **Herstel standaard vensterlayout**

## Eerder toegevoegd in 3.1.46
- Crashfix: laden van een `.aeo` project faalt nu veilig wanneer de bronbestanden ontbreken of verplaatst zijn.
- Bij ontbrekende bron verschijnt nu een duidelijke foutmelding i.p.v. app-crash.
- Projectinstellingen worden niet meer verder toegepast als de bron niet succesvol kan worden geopend.
- Extra null-guards toegevoegd in de project-load flow om dereference-crashes te voorkomen.

## Eerder toegevoegd in 3.1.45
- Fix: **Reset Section** laadt nu opnieuw de opgeslagen defaults uit **Set as Default** i.p.v. harde fabriekswaarden.
- Geldt voor:
  - Soundtrack / Frame Pitch / Video-crop sectie
  - Image Processing sectie
  - Extract / Audio / Output sectie
- Resultaat: “Set as Default” en “Reset Section” werken nu logisch samen zoals verwacht.

## Eerder toegevoegd in 3.1.44
- Preferences vergroot nu automatisch extra in hoogte wanneer de General-opties niet volledig in beeld passen.
- Save/Discard-zone en tabgebied schalen daarbij consistent mee.
- Resultaat: de opties kunnen doorlopen tot aan de onderrand van het subpaneel (geen onnodige verticale slider als schermruimte volstaat).

## Eerder toegevoegd in 3.1.43
- Embedded preview-dock krijgt nu automatisch een betere breedte op basis van bronverhouding.
- Nieuwe voorkeur in **Preferences**: **Lock preview aspect strictly (embedded preview)**.
- Met strict lock actief blijft de embedded previewbreedte hard vergrendeld op een ratio-veilige waarde.
- Zonder strict lock blijft een veilige minimum-breedte actief zodat de preview niet tot een smalle strook inzakt.
- Aspect-bescherming wordt ook opnieuw toegepast bij hoofdvenster-resize en na aanpassen van Preferences.

## Eerder toegevoegd in 3.1.42
- Hotfix: Preferences forceert nu een bruikbare minimumhoogte, ook wanneer een oudere (te kleine) opgeslagen venstergrootte wordt hersteld.
- Onderpaneel met Save/Discard en het tabgebied schalen nu consequent mee met de effectieve dialoghoogte.
- Resultaat: geen afgedekte instellingen meer onderaan in de General-tab.

## Eerder toegevoegd in 3.1.41
- Fix: General-instellingen raken niet meer verborgen achter het subpanel-kader.
- General-tab gebruikt nu een scrollbare inhoud zodat extra opties altijd bereikbaar blijven.
- Preferences opent standaard hoger voor betere zichtbaarheid van opties.
- Preferences onthoudt nu de laatst gebruikte venstergrootte en -positie.

## Eerder toegevoegd in 3.1.40
- Nieuwe voorkeur in **Preferences**: **Preview insluiten in hoofdpaneel (beta, herstart aanbevolen)**.
- Embedded preview opent als subpaneel (dock) in het hoofdvenster als deze optie actief is.
- In embedded mode is een subtiele **BETA**-header met tooltip toegevoegd.
- Nieuwe kleurinstelling voor de BETA-badge:
  - **Automatisch op basis van thema**
  - Paars / Blauw / Groen / Oranje / Rood / Grijs
- Badgekleur wordt nu **live** bijgewerkt na themawissel (zonder bron heropenen).
- Eerste opstart blijft HD-vriendelijk met split-workflow als veilige standaard.

## Eerder toegevoegd in 3.1.39
- Stabiliteitsfix voor hulplijnen bij Frame Pitch afregeling:
  - de **paarse lijn** volgt nu voorspelbaar de manuele Frame Pitch End-slider
  - geen onverwachte grote sprongen meer door auto-detect herpositionering
- Nieuwe view-optie:
  - **Show Auto-Detect Line** toont een aparte **groene** auto-detect referentielijn
- Nieuwe mini-legenda in View options met kleurbetekenis:
  - wit = Pitch Start
  - paars = Pitch End
  - groen = Auto-detect
  - cyaan/oranje = Video Top/Bottom

## Eerder toegevoegd in 3.1.38
- CI buildfix voor Windows/Qt5 strict compile:
  - report-schrijfmethodes niet langer `const` zodat intern loggen (`Log()`) geldig compileert
  - lost de CI-fout op (`passing 'const MainWindow' as 'this' argument discards qualifiers`)

## Eerder toegevoegd in 3.1.37
- Qt5-compatibiliteitsfix in `mainwindow.ui`:
  - `contentsMargins`-blok vervangen door expliciete `left/top/right/bottomMargin` properties
  - lost de CI-fout op (`uic: Unexpected element left`) tijdens Windows package-build

## Eerder toegevoegd in 3.1.36
- Queue batch processing:
  - **Pause/Resume** tijdens lopende batch
  - **Skip current** item zonder batch volledig te stoppen
  - veiligere batch-afhandeling met guard tegen dubbel starten en exception-safe reset
- Uitgebreide batchrapportering:
  - status + rapporten met **processed/pending/success/failed/skipped**
  - **min/max/gemiddelde** tijd per item + topsnelheidsindicatie
  - fouttypes met aantallen, gesorteerd op frequentie, inclusief **top failure**
- Veel rijkere batch-resultaten popup:
  - acties voor **retry/requeue** (gefaalde, overgeslagen, of alle niet-gelukte)
  - directe acties: **open laatste rapport**, **open rapportmap**, **kopieer rapportpaden**
  - kopieeracties: **statusregel**, samenvatting, fouttypes, details, JSON en volledig **support-pakket**
- Betere support-output:
  - JSON en tekst bevatten nu rapportpaden + rapportmap
  - runtime metadata toegevoegd: **appversie, Qt-versie, OS**
  - duidelijkere feedback bij queue-limiet (geaccepteerd vs. afgevallen items)

## Eerder toegevoegd in 3.1.35
- Nieuwe voorkeur in **Preferences**: **License agreement na eerste acceptatie niet meer tonen**.
- Nieuwe **Exportprofielen per doel** in Video instellingen:
  - **Snelle review**
  - **Web share**
  - **Archiefkwaliteit**
  - + automatische terugval naar **Aangepast** bij manuele wijzigingen.
- Uitgebreide rapportering:
  - single-export rapporten (**TXT/JSON**) met settings, outputpaden en duur
  - queue-batch rapporten uitgebreid met totale duur + gemiddelde tijd per item
- Queue batch verbeteringen:
  - live voortgang met **% + ETA**
  - eindstatus met **totale duur + gemiddelde tijd/item**
  - per-item detailregels met **framebereik + duur + foutreden**
  - foutpopup met:
    - retry van gefaalde items
    - kopieer details naar klembord
    - bewaar details als tekstbestand
    - samenvatting per fouttype met aantallen
- Extra export-waarschuwingen:
  - ontbrekend video-outputpad
  - container/extensie mismatch
  - FPS-mismatch tussen bronhint en gekozen frame rate

## Eerder toegevoegd in 3.1.34
- Presets/workflow:
  - instellingen-presets opslaan/laden/verwijderen
  - knop voor laatst gebruikte instellingen
  - reset per sectie verbeterd
- Video-export gemak:
  - output bestandsnaam-template met tokens (bv. `{source}_{datetime}`)
  - keuze **automatisch nummeren** of **overschrijven**
  - optie **open map na export**
- Test Run:
  - extra presets (**5 sec / 20 sec / 1 min**)
  - loop test-run met instelbaar aantal herhalingen
  - test zonder definitief wegschrijven (tijdelijke bestanden)
- UI/bruikbaarheid:
  - compacte UI-modus
  - subtiele help-iconen met tooltip
  - zoekveld in Preferences om opties snel te filteren
- Veiligheid/kwaliteit:
  - preflight check op schijfruimte voor export
  - FFmpeg testknop + duidelijke status
  - basis integriteitscheck na export
- Queue:
  - queue-item dupliceren
  - queue-item omhoog/omlaag verplaatsen
  - **stop na huidig item** bij batch-extractie

## Eerder toegevoegd in 3.1.33
- Nieuwe voorkeur in **Preferences**: **Toon helptekst in de app** (geldt app-breed).
- Tabnaam aangepast naar **Audio- & Video-track Settings**.
- Actieve tab heeft nu een duidelijkere highlight-kleur (licht en donker thema).

## Eerder toegevoegd in 3.1.32
- Helptekst bij Audio kwaliteit is subtieler gemaakt (kleiner + grijzer).
- Nieuwe checkbox **Toon helptekst** om de audio-helpregels te verbergen/tonen.
- Voorkeur voor helptekst-zichtbaarheid blijft bewaard na herstart.

## Eerder toegevoegd in 3.1.31
- Ultra-korte push/pull tuningtip onder Frame Pitch / Search Size voor sneller afregelen.

## Eerder toegevoegd in 3.1.30
- Adaptieve hoofd-UI-schaal is teruggezet naar de vaste standaardlayout om leesproblemen te vermijden.
- Previewvenster-layoutinstellingen (single/dual monitor gedrag) blijven behouden.
- Vensterstatus (positie/grootte/maximized/fullscreen) blijft bewaard na herstart.

## Eerder toegevoegd in 3.1.29
- UI-inhoud schaalt nu mee met de effectieve vensterruimte (ook buiten fullscreen), zodat lege ruimte beter benut wordt.
- Tekstgroei werd veiliger gemaakt (sub-lineair, met limiet) om overlap te voorkomen.
- Schaal activeert pas bij zinvolle venstergroei boven de basismaten.

## Eerder toegevoegd in 3.1.28
- Grote UI-schaal wordt nu alleen toegepast in fullscreen-achtige layout-situaties.
- In normale venstermodus blijft de UI op standaardgrootte om overlap van tekst/controls te vermijden.
- Vensterstatus (positie/grootte/maximized/fullscreen) wordt nu bewaard en hersteld na herstart.

## Eerder toegevoegd in 3.1.27
- In split/fullscreen monitor-layouts werd de minimale UI-breedte verhoogd zodat het UI-venster op 4K niet te smal wordt.
- Hierdoor wordt de bestaande tekst- en sliderschaal in de praktijk beter zichtbaar en bruikbaar.

## Eerder toegevoegd in 3.1.26
- In fullscreen monitor-layouts schalen hoofdpanelen in het UI-venster proportioneel mee zodat de beschikbare ruimte beter benut wordt.
- Tekst en slider-controls schalen nu mee in grote vensters voor betere leesbaarheid.
- Schaal wordt direct opnieuw toegepast na aanpassen van vensterlayout-instellingen in Preferences.

## Eerder toegevoegd in 3.1.25
- 1-monitor split gebruikt nu de schermruimte slimmer zodat UI en preview zo veel mogelijk **even hoog** blijven, met behoud van de film-beeldverhouding.
- In 1-monitor split kan de breedte van het UI-venster dynamisch variëren om de preview-verhouding correct te houden.
- Adaptieve UI-schaal toegevoegd: bij groter UI-venster schalen tekst en sliders mee op.

## Eerder toegevoegd in 3.1.24
- **Exit** toegevoegd in het **File** menu (met **Ctrl+Q**).
- Nieuwe **Vensterlayout**-modus in Preferences:
  - **Standaard (monitor-onafhankelijk)**
  - **1 monitor split (UI + preview)**, ook bruikbaar op systemen met 2 monitoren via **Doelmonitor**
  - **2 monitoren (gescheiden)** met **Swap vensters tussen monitor 1 en 2**
- Eenvoudig terugkeren naar de standaard vrije layout via Preferences.

## Eerder toegevoegd in 3.1.23
- Nieuwe **Resolutie**-keuze in Video instellingen tot en met **4K UHD**.
- Nieuwe vensterindeling-opties in Preferences:
  - bij 2 monitoren: hoofdvenster op scherm 1, preview op scherm 2
  - bij 1 monitor: split-layout die het volledige HD/UHD-scherm benut
- Nieuwe presets voor audiokwaliteit:
  - **Audio kwaliteit** (met video-audio bitrate)
  - **Audio only kwaliteit** (enkel sampling rate + bit depth, los van video)

## Eerder toegevoegd in 3.1.22
- Progress pop-up toont nu **verstreken tijd**, **verwacht totaal** en **resterende tijd** tijdens wegschrijven van WAV en/of MP4.
- Knoppen **Test Run** en **Extract** hebben nu een verschillende kleur zodat de acties direct duidelijk zijn.

## Eerder toegevoegd in 3.1.21
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

## Ultra-korte package checklist (5 punten)
- [ ] Release-tag = `v3.1.52`.
- [ ] Asset `AEO-Light-Windows-x64.zip` staat op de release.
- [ ] ZIP uitgepakt naar een lokale map.
- [ ] `AEO-Light.exe` start en toont versie **3.1.52**.
- [ ] Korte **Test Run** slaagt en maakt output aan.

## Package checklist (v3.1.52, Windows x64)
- [ ] Release-tag is `v3.1.52`.
- [ ] Asset `AEO-Light-Windows-x64.zip` is aanwezig op de releasepagina.
- [ ] ZIP uitgepakt naar een lokale map (niet rechtstreeks uit de ZIP starten).
- [ ] In de uitgepakte map staan minimaal:
  - `AEO-Light.exe`
  - `LICENSE.txt`
  - `ffmpeg.exe`
- [ ] App start zonder crash en toont in de titel/over dialoog versie **3.1.52**.
- [ ] Snelle rooktest:
  - bron laden
  - korte **Test Run** uitvoeren
  - outputbestand wordt aangemaakt
- [ ] (Optioneel) checksumcontrole van download:
  - `certutil -hashfile AEO-Light-Windows-x64.zip SHA256`
  - verwacht: waarde op de `v3.1.52` releasepagina

## Snelle start (aanbevolen)
- Container: **MP4**
- Preset: **Standaard**
- FPS: gelijk aan je bronmateriaal
- Bitrate: begin rond **8–12 Mbps** en verhoog alleen indien nodig
