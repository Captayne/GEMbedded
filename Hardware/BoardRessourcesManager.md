# GEMbedded Board Ressources Manager

**Stand:** 03.10.2026
**Zweck:** Festhalten, wie GEMbedded auf beliebigen Brettern konfigurierbar wird — und was von den Ressourcen danach noch dem Anwender gehört.
**Status:** Entwurf aus einem Brainstorming. **Nichts davon ist implementiert.**

**Herkunft der Zahlen.** Jede Pinnummer in diesem Dokument gehört in eine von
vier Klassen, und wer sie benutzt, sollte wissen in welche:

| Klasse | heißt | Beispiel |
| --- | --- | --- |
| **Chip** | aus dem SVD oder den SDK-Headern abgeleitet | HSTX nur auf GP12–19 |
| **Treiber** | aus der Quelle des laufenden Codes abgelesen | `LCD_SCK 10` |
| **Datenblatt** | aus Herstellerunterlagen übernommen | EP5008S-Porches |
| **Entwurf** | ausgedacht, von keiner Hardware bestätigt | das Carrier-Pinout |

Die ersten drei kann man verdrahten. Die vierte nicht.

Verwandte Dokumente:

- `Display+Carrier/GEMbedded_EP5008S_ST7701S_RGB565_Bringup_Notes.md` — Timing
  und Init aus Herstellerunterlagen (**Datenblatt**), Verdrahtung darin **Entwurf**
- `Display+Carrier/GEMbedded_RP2350B_Carrier_RevA_Pinout V2.md` — **Entwurf,
  von keiner Platine bestätigt.** Nicht danach verdrahten.
- `../ROADMAP/ROADMAP.md` — Abschnitte zur Ressourcenampel und zur Arduino-Anbindung

---

# 1. Das Ziel

GEMbedded soll für **beliebig viele verschiedene DIY-Projekte** auf **immer neuen Brettern** laufen. Daraus folgt eine Frage, an der alles andere hängt:

> **Was muss ein Fremder tun, damit GEMbedded auf seinem Brett läuft?**

Drei ehrliche Antworten, von denen eine gewählt werden muss:

1. unseren Toolchain aufsetzen und neu kompilieren — *heute der Fall*
2. eine Textdatei ändern und neu flashen
3. eine Beschreibung übertragen, ohne Compiler

Für DIY ist (1) das Ende der Sache, bevor sie anfängt. Ziel ist (3), und zwar nicht für alles gleichzeitig — siehe Abschnitt 2.

## 1.1 Das zweite Ziel

Wer das Brett konfiguriert hat, **weiß damit auch, was belegt ist**. Also kann dasselbe Werkzeug dem Anwender sagen, was ihm für WS2812, Sensoren und eigene Protokolle noch bleibt. Konfigurator und Ressourcenampel sind dasselbe Programm in zwei Betriebsarten:

```
Schreiben:  Display, Pins, Busse einstellen
Lesen:      Ampel — was ist frei, was bedingt frei, was gesperrt
```

## 1.2 Wozu das zusammen führt

Vier Dateiarten, alle Text, alle verschickbar:

```
board.gem       was wo verdrahtet ist     <- der Anwender, einmal pro Brett
panel.seq       Chip-Sequenz + Timing     <- einmal pro Glas, von irgendwem
resources.h     was belegt ist            <- generiert, für den Compiler
*.prg / sketch  das Programm              <- der Anwender, so oft er will
```

Ein Ökosystem entsteht nicht durch Funktionen, sondern dadurch, dass **die Datei eines Fremden auf meiner Maschine funktioniert**. Das leisten die ersten beiden: wer ein Panel zum Laufen gebracht hat, schickt eine `.seq`; wer ein Brett beschrieben hat, eine `board.gem`. Ohne Compiler, ohne unser Zutun, ohne Firmware-Release.

Das Fundament darunter ist der eigentliche Startvorteil: **µST ist eine Programmierschnittstelle mit vierzig Jahren Literatur.** VDI und AES sind dokumentiert, Dialoge und Fenster existieren, und ein GEM-Programm von 1987 läuft auf dem Panel. Jede neue Embedded-GUI muss ihre API erst erfinden, erklären und bewerben.

---

# 2. Die Startreihenfolge bestimmt die Schichtung

Das ist keine Geschmacksfrage. Was wann gebraucht wird, legt fest, wo es liegen darf:

```
QSPI / PSRAM-CS   ──► muss stehen, bevor überhaupt Code läuft
Board-Pins        ──► der SD-Treiber braucht sie  ──► kann NICHT von SD kommen
Panel + Timing    ──► vor dem Desktop, nach dem SD ──► könnte von SD kommen
Eingabegeräte     ──► später                        ──► SD oder EMUDESK.INF
```

**Kernaussage:** Die Boardbeschreibung kann nicht auf der SD-Karte liegen, weil die Pins der SD-Karte darin stehen. Und der SD-Pfad ist ausgerechnet der, der heute schon gelegentlich hustet (grüner Desktop = Einstellungen ungelesen). Eine Beschreibung, deren Ausfall den Bildschirm kostet, über den man den Ausfall sehen würde, ist eine schlechte Wette.

Die WiFi-Zugangsdaten reiten heute in **EMUDESK.INF** (`pTOS/desk/deskapp.c:981`). Das ist der richtige Ort für *späte* Dinge — aber dort läuft der Desktop schon, der Bildschirm ist längst da. Für das Display kommt es Stunden zu spät.

---

# 3. Der Speicher existiert bereits

`pTOS/bios/machine/rp2350/rp2350_nvram.c`:

- **zwei 4-KB-Sektoren** oben im Flash, abwechselnd beschrieben
- Magic `'PSET'`, Sequenznummer, Länge, Prüfsumme
- **versionstolerant**: Felder werden hinten angefügt, nie verschoben; ein kürzerer Satz ist einer von einer älteren Version, und der Rest nimmt seinen Standardwert
- **kein Laufwerk, kein Dateisystem, für kein Programm erreichbar**
- Lesen kostet nichts, weil der Satz im XIP-Fenster liegt

Der Header sagt selbst, worauf er wartet:

> *It starts nearly empty on purpose. What belongs here — screen orientation, the time zone, the radio, **which pins the system has spoken for** — is a decision about the machine and not about this file, and each of those wants its own thought.*

**Das BIOS-Flash ist also fertig.** Was fehlt, ist die Konsole davor und die Liste der Felder.

## 3.1 Richtigstellung: kein internes Flash

Der RP2350 hat **kein internes Flash**. Nur 8 KB OTP, einmalig programmierbar, als Konfigspeicher unbrauchbar. Die 16 MB auf dem Brett sind externes QSPI-Flash.

Richtig ist aber: Flash und PSRAM hängen am **selben QMI-Bus** — gemeinsamer Takt, gemeinsame Datenleitungen, getrennte Chip-Selects. Beide laufen zusammen (das ist die vorgesehene Bestückung), aber sie teilen die Bandbreite.

## 3.2 Die 23-Millisekunden-Regel

Aus `rp2350_nvram.c`:

> *every write to this flash takes both QSPI chip selects away for 23 milliseconds — the PSRAM with it (rp2350_flash.c).*

Beim EP5008S scannt der DMA in dieser Zeit aus dem PSRAM. 23 ms sind bei 30 Hz etwa **zwei Dreiviertelbilder, die nicht kommen**: der Panel-DCLK läuft weiter, das FIFO läuft leer, danach sitzt das Bild schief.

**Daraus folgt: Speichern gehört vor den Displaystart.** Nicht aus Bequemlichkeit, sondern weil es der einzige Moment ist, an dem es nichts kostet. Soll später doch im Betrieb gespeichert werden, muss das Panel vorher angehalten und danach neu aufgesetzt werden — bewusst und sichtbar.

---

# 4. Das BIOS-Setup

## 4.1 Eintritt

| Lage                           | Verhalten                                                           |
| ------------------------------ | ------------------------------------------------------------------- |
| kein gültiger Satz im Flash    | **direkt ins Setup**, mit einer Zeile, die sagt warum. Kein Warten. |
| gültiger Satz, Taste gehalten  | Setup                                                               |
| gültiger Satz, nichts passiert | nach Wartezeit normal booten                                        |

**Nicht die Uhr zählen, sondern auf das Öffnen des Ports warten.** USB-CDC ist in Sekunde 0 noch nicht da; Enumeration und das Öffnen durch den Host brauchen ihrerseits etwa eine Sekunde. Wer sich anhängt, nachdem das Fenster zu war, sieht sonst nur Stille.

**Zusätzlich eine Tür aus Hardware.** Eine gehaltene Taste ist der einzige Weg hinein, der auch funktioniert, wenn USB verwirrt ist. **Welcher Pin, ist offen** — die bisherige Antwort (`GP23 User Button`) stammt aus dem Carrier-Entwurf und ist damit keine. Am echten Brett nachsehen; hat es keine Taste, bleibt nur der USB-Weg.

**Die Wartezeit ist ein Feld.** `0` = nie warten, Eintritt nur über die Taste. Ein Maschinenbedienpanel oder eine Waschmaschine darf nicht fünf Sekunden warten, ob jemand etwas tippen will. Tischgerät 5 s, eingebautes Gerät 0.

## 4.2 TRY und SAVE

Der Client schiebt eine Konfiguration hinüber, das Brett **startet damit, ohne etwas ins Flash zu schreiben**. Erst wenn ein Bild steht, sagt der Anwender speichern.

Nebeneffekt, der wichtiger ist als er klingt: **eine schlechte Probekonfiguration verschwindet beim nächsten Einschalten von allein.** Man kann sich nicht aussperren, egal wie falsch man rät — und beim Displaywechsel ist falsches Raten der Normalfall.

Der Code trennt das schon, ohne es zu wissen: es gibt `live` (was gilt) und den Satz im Flash (was bleibt). Setup füllt `live`, bootet damit, und `rp2350_nvram_put()` passiert erst auf ausdrückliches „Save".

Der Tastendruck bedeutet damit auch nicht „zeig mir ein Menü", sondern nur: **nicht booten, auf den Client warten.**

## 4.3 Zur Laufzeit nicht erreichbar

Entscheidung: nach dem Booten **nur anzeigen, nie schreiben**. Dann ist die 23-ms-Regel eine Regel und keine Ausnahme, und es gibt keinen Streit um den seriellen Port, weil Setup und Betrieb sich nie überlappen.

---

# 5. Der PC-Client

`tools/gembios.py`, in der Familie von `gemflash.py`, `gemdeploy.py`, `gemshot.py`, `gemview.py`, `gemwipef.py`.

## 5.1 Warum nicht ein Menü auf dem Brett

Weil damit das Wissen dorthin wandert, wo es ohne Neuflashen gepflegt werden kann:

> **Ein neues Panel wird ein Tabelleneintrag in Python, keine Firmwareänderung.**

Im BIOS entsteht dadurch **gar nicht erst**: Menü, Zeileneditor, Hilfetexte, Panelkatalog, DCLK-Rechnerei, Plausibilitätsprüfung. Stattdessen vier Verben — *gib mir deine Konfig*, *hier ist eine neue*, *starte damit*, *behalte sie*. Eher 150 Zeilen C als 800, an der Stelle, an der Codegröße und Robustheit am teuersten sind.

## 5.2 Was nur der PC kann

- **Prüfen, bevor es weh tut:** Pin doppelt belegt; PSRAM-CS nicht auf 0/8/19/47; GP47 auf einem RP2350A; Framebuffer größer als das gefundene PSRAM; DCLK aus H/V-Totalen gerechnet statt getippt.
- **Katalog:** „EP5008S / ST7701S 480×854" mit Porches, Polaritäten und Registersequenz — einmal richtig eingetragen, für alle richtig.
- **Profile als Dateien:** eine Konfiguration pro Brett, im Git, versioniert, per Mail verschickbar. „Schick mir deine Konfig" ist für eine Bastlerplattform mehr wert als jedes Menü.

## 5.3 Die eine Entscheidung, die vor der ersten Zeile fallen muss

**Wer kennt das Satzformat?**

- **A — der Client kennt die Struktur.** Einfach. Bricht bei jeder Firmware, die ein Feld hinzufügt; ein alter Client schreibt dann Unsinn in einen neuen Satz.
- **B — das Brett beschreibt seine Felder selbst** (Name, Typ, Länge). Der Client kann eine Konfiguration bearbeiten, **die er nie gesehen hat**. Kostet ein paar hundert Bytes Namen im Flash und macht den Client für immer vorwärtskompatibel.

**Empfehlung: B.** Arbeitsteilung: das Brett ist Autorität über das **Format**, der PC über die **Bedeutung**. `rp2350_nvram.c` passt dazu schon — es nimmt die Länge aus dem Satz und nicht aus seiner eigenen Vorstellung.

## 5.4 Das Netz: ein Terminal hat jeder

`gembios.py` hat nur, wer es installiert hat. Deshalb bleiben auf dem Brett ein paar Klartextzeilen:

```
GEMbedded BIOS 1 -- no valid configuration
display: none    waiting for gembios.py on this port
type ? for the few things this console can do itself
```

Hinter `?` genau **zwei** Verzweiflungstaten: *Konfig zeigen* und *Defaults laden und starten*. Vierzig Zeilen, und der Unterschied zwischen „ich frage im Forum" und „ich helfe mir selbst".

Begrüßung menschenlesbar, Protokoll maschinenlesbar, dieselbe Leitung.

## 5.5 Praktisches

- Nur ein Programm darf den COM-Port halten. `gembios.py` muss sagen: „bitte gemview schließen".
- Falls es später eine Oberfläche braucht: `tools/Connecty/` macht Python-Backend plus `workbench.html` schon genau so. Erst aber Kommandozeile.

---

# 6. Die Ressourcenampel

## 6.1 Warum die Fassung in der ROADMAP kippt

Das Skript ab `ROADMAP.md:1339` leitet die Belegung aus Arduino-Menüpunkten ab:

```python
if is_rgb:
    used_pio_sm += 2   # 2 State Machines für HSYNC/VSYNC/Pixel-Clock
    used_dma_ch += 1
```

Das ist eine **Schätzung in einem Skript, parallel zur Firmware**. Braucht der RGB-Treiber später drei State Machines, sagt der Report weiter „2". Dann ist die Ampel nicht ungenau, sondern **unglaubwürdig** — und beim ersten „grün" über einen Pin, der dem Display gehört, benutzt sie keiner mehr.

Zwei solche Risse stecken schon drin: der Report nennt „EP5008S 24-Bit RGB Panel", während `FORMAT=RGB565` 16 Leitungen bedeutet, und er reserviert „PIO-Block 0 komplett", zählt aber 2 von 12 SMs.

**Der BIOS-Server muss nicht schätzen, er kann antworten.**

## 6.2 Die eine Sache, die jetzt anfangen muss

Damit das Brett antworten *kann*, muss jeder Treiber sagen, was er nimmt — **in dem Moment, in dem er es nimmt**:

```c
res_claim(RES_GPIO, 17, "EP5008S DCLK");
res_claim(RES_PIO_SM, PIO1_SM0, "RGB pixel stream");
res_claim(RES_PIO_INSTR(1), 9, "RGB pixel stream");
res_claim(RES_DMA, 3, "framebuffer -> PIO1");
```

Eine Struktur und ein Aufruf. Kostet fast nichts — **aber nur jetzt.** Nachträglich heißt: dreißig Treiber durchgehen und raten, und jeder künftige Treiber vergisst es. Vorher heißt: es ist einfach wahr, für immer, ohne Pflege.

Der Zeitpunkt ist gut: Display und Touch sind gerade getrennt worden (`rp2350_lcd.c` / `rp2350_touch.c`). Das sind die ersten zwei Anmelder statt eines Klumpens.

## 6.3 GPIO ist der leichte Teil

| Ressource                    | Vorrat                          | Anmerkung                               |
| ---------------------------- | ------------------------------- | --------------------------------------- |
| GPIO                         | 48                              | der sichtbare, einfache Teil            |
| DMA-Kanäle                   | 16                              | Scanout, SD, später Audio               |
| PIO State Machines           | 12                              | steht schon in der ROADMAP              |
| **PIO-Instruktionsspeicher** | **32 pro Block**                | **das, was tatsächlich zuerst ausgeht** |
| PWM-Slices                   | 12 (24 Kanäle)                  | Backlight nimmt einen                   |
| Festperipherie               | 2×UART, 2×SPI, 2×I²C, HSTX, USB | nicht teilbar                           |
| QSPI-Chip-Selects            | 2                               | Flash + PSRAM, fertig                   |
| SRAM / PSRAM                 | 520 KB / je nach Brett          | Framebuffer frisst 800 KiB              |
| Core 1                       | 1                               | gehört rtcore                           |

Der Instruktionsspeicher ist der Klassiker: vier State Machines teilen sich **32 Instruktionen**. Ein RGB-Scanout kann einen Block praktisch leerräumen, obwohl drei SMs „frei" sind. WS2812 braucht dann vier Instruktionen — und findet keine. Eine Ampel, die SMs zählt und Instruktionen nicht, sagt in genau diesem Fall grün.

## 6.4 Die dritte Farbe ist die wertvollste

Rot/grün ist nur Subtraktion. Den Nutzen bringt **gelb: frei, aber mit Bedingung.**

```
GP38  [GRUEN]  frei          PWM, PIO, SPI1 TX, ADC nein
GP28  [GELB ]  frei, aber    letzter Pin, der I2C0 SDA tragen kann
GP11  [GELB ]  frei, aber    mitten im RGB-Bus, schaltet mit 14 MHz
                             -> nichts Analoges, nichts Hochohmiges
GP17  [ ROT ]  EP5008S DCLK
```

Gelb kann nur der PC sagen, weil dazu die Pin-Funktions-Matrix gehört. Die wohnt in `gembios.py`, nicht im BIOS.

## 6.5 Eine Tabelle, drei Abnehmer

Der eigentliche Gewinn der Zusammenlegung — die ROADMAP will drei Dinge, die sonst drei Quellen hätten:

1. **Die Ampel** im Board Ressources Manager (`ROADMAP.md:1043` ff.)
2. **Der Ressourcen-Schild zur Compilezeit** (`ROADMAP.md:632` ff.): `gembios.py --export-arduino` holt die *echte* Belegung und schreibt den Header, gegen den `static_assert` prüft. Dann prüft es gegen die Maschine und nicht gegen ein Dropdown.
3. **Die Laufzeitabsage** (`ROADMAP.md:379`: *„die App meldet konkret, was nicht geht, und terminiert"*) — dieselbe Liste, im laufenden System abfragbar.

## 6.6 Ohne Brett benutzbar

Der Lese-Teil soll auch gegen eine gespeicherte Profildatei laufen. Dann kann jemand planen, *bevor* er kauft: „Waveshare + EP5008S — bleibt genug für zwölf WS2812 und zwei I²C-Sensoren?"

Lesen darf nie etwas im Flash anfassen. Dann kann man es bedenkenlos offen stehen lassen.

---

# 7. Aufwandsarm anfangen

## 7.1 Die Chip-Tabelle nicht abtippen

Sie steht maschinenlesbar auf der Platte:

```
C:\Installed\pico-sdk\src\rp2350\hardware_regs\RP2350.svd
```

Für **jeden** der 48 Pins sind alle Funktionen mit Namen enumeriert — **HSTX inklusive**. Eine Tabelle, die aus dem SVD fällt, kann HSTX nicht vergessen.

Probeläufe (03.10.2026, nur Scratchpad, nichts angelegt): 48 Pins, vollständig.

```
xip_ss_n_1 (PSRAM-CS) möglich auf:  0, 8, 19, 47
hstx möglich auf:                   12 .. 19
i2c0_sda möglich auf:               0, 4, 8, 12, ... 44
```

Drei Befunde, die den Weg rechtfertigen:

- Die erste Zeile ist **genau**, was heute als Prosa in `pTOS/bios/Kconfig:479` steht („0, 8, 19 or 47"). Künftig abgeleitet statt behauptet.
- Die zweite fehlt im Pinout-Bild. Und sie ist nicht eine von mehreren Lagen, sondern **die einzige**: wer GP12–19 für etwas anderes ausgibt, hat HDMI für dieses Brett dauerhaft verkauft. Siehe 13.7.1.
- Die dritte ist der Grund, warum „letzter Pin, der I²C0 SDA tragen kann" überhaupt berechenbar ist.

Form der Tabelle:

```
GP  F0           F1         F2        F3        F4       F9/F11
0   jtag_tck     spi0_rx    uart0_tx  i2c0_sda  pwm_a_0  xip_ss_n_1
12  hstx_0       spi1_rx    uart0_tx  i2c0_sda  pwm_a_6  clocks_gpin_0
17  hstx_5       spi0_ss_n  uart0_rx  i2c0_scl  pwm_b_0  -
47  -            spi1_tx    uart0_rts i2c1_scl  pwm_b_11 xip_ss_n_1
```

## 7.2 Zwei Dateien und eine Subtraktion

**Datei 1 — der Chip. Boardunabhängig, generiert, nie von Hand angefasst.**
`tools/rp2350_pins.csv`, 48 Zeilen, dazu ein Generator von ~20 Zeilen. Regenerierbar, wenn ein SDK-Update etwas korrigiert.

**Datei 2 — das Brett. Von Hand, klein, erstmal flach und dumm.**

```
# board: waveshare-rp2350b-pizero
psram.cs      = 47
sd.spi0       = miso=32 cs=33 sck=34 mosi=35
esp.uart0     = tx=36 rx=37
display.kind  = spi-ili9341
display.spi1  = sck=.. mosi=.. cs=.. dc=.. rst=..
touch.kind    = xpt2046
touch.spi0    = int=.. cs=..
```

Die Ampel ist dann: `alle Pins − beanspruchte Pins`, mit Datei 1 als Spalte „was kann der Pin noch".

Das ist die ganze erste Fassung: **keine Firmwareänderung, kein Protokoll, kein BIOS-Server** — und sie beantwortet „was habe ich noch für WS2812" für unsere Bretter schon heute. Registrar und BIOS-Server machen die Antwort später *wahr*; die Tabelle macht sie jetzt *nützlich*.

## 7.3 Reihenfolge, billigstes zuerst

|     | Schritt                                       | Aufwand               | Nutzen sofort                             |
| --- | --------------------------------------------- | --------------------- | ----------------------------------------- |
| a   | Chip-Tabelle aus dem SVD generieren           | Minuten               | HSTX-Pins, CS-Pins, Funktionsmatrix       |
| b   | Boardprofil Waveshare **von Hand**            | ~1 h                  | zwingt uns aufzuschreiben, was belegt ist |
| c   | `gembios.py --show` liest beide, druckt Ampel | klein                 | die Ampel, offline                        |
| d   | `res_claim()` in den Treibern                 | klein, **aber jetzt** | Profil gegen die Realität prüfbar         |
| e   | BIOS-Server + Flash-Konfiguration             | groß                  | ein Image für alle Bretter                |

Jeder Schritt ist für sich brauchbar, keiner blockiert den nächsten. (b) zahlt doppelt: was für den Waveshare aufgeschrieben wird, brauchen wir für den WeAct sowieso — und **die Differenz der beiden Profile *ist* die Beschreibung**, nach der wir suchen. Abgelesen statt erfunden.

## 7.4 Zwei Disziplinen

**Das WeAct-Profil vor dem Löten schreiben**, nicht danach. Dann ist es eine Vorhersage, die der Bringup bestätigt oder korrigiert, und kein Protokoll, das hinterher niemandem mehr hilft.

**Den „Displaybaum" noch nicht als Baum schreiben.** Bei drei Arten reicht `display.kind =` plus flache Schlüssel; Verschachtelung kostet sofort einen Parser mit Zuständen. Die Struktur darf kommen, wenn es fünf Arten sind und man sieht, wo sie wirklich verzweigt.

**Die Arduino-IDE-Anbindung aus der ROADMAP offen lassen.** `boards.txt`-Permutationen, generierte Beispiel-Sketches, Hooks in `platform.txt` — viel Arbeit in einem fremden Buildsystem, das mit jeder IDE-Version veraltet. Der **Export** dagegen ist klein und sofort nützlich: eine Header- und eine Textdatei.

---

# 8. Was beschrieben wird und was erfragt wird

Nicht alles muss in der Beschreibung stehen. Die Haltung gibt es im Baum schon: `initinfo` liest die **PSRAM-Größe aus dem Chip** statt aus einer Tabelle.

| erfragbar            | Weg                                       |
| -------------------- | ----------------------------------------- |
| PSRAM-Größe und -Typ | antwortet auf dem QMI, wird schon gelesen |
| Touchcontroller      | GT911 nennt auf I²C seine Kennung         |
| HDMI-Monitor         | EDID über DDC                             |
| ~~Panel oder HDMI~~  | ~~`DISPLAY_MODE_ID`-Leitung~~ — Entwurf, und unnötig: siehe 13.7.1 |

**Die Beschreibung trägt nur, was sich nicht erfragen lässt: die Verdrahtung.** Alles andere macht sie länger und falscher.

---

# 9. Was getippt wird und was nicht

Pins, Auflösung, Porches, Orientierung, Spiegelung: Punkt für Punkt, gern.

Aber die **ST7701S-Initialisierung sind ~400 Bytes Registerdaten**. Die tippt niemand durch ein Menü, und ein Tippfehler darin ist unsichtbar. Also liegen die bekannten Panels **im Image** als benannte Einträge, und der Satz speichert den Namen plus Abweichungen:

```
Display
  1) ILI9341      320x240  SPI
  2) ST7701S      480x854  RGB565 parallel     <- EP5008S
  3) DVI/HSTX     640x480
  4) custom       (Sequenz als Hex einfügen)
```

Punkt 4 ist das eine, worin ein Terminal wirklich gut ist: einen Blob einfügen.

## 9.1 Init-Sequenzen sind Daten, nicht Code

Das Teuerste an einem neuen Panel sind nicht die Pins, sondern die ~200 Registerschreibvorgänge. Und die liegen in `Display+Carrier/Displays/EP5008S/EP5008S初始化.c` schon in der Form vor, die sich mechanisch übersetzt:

```
FF 05 77 01 00 00 10      Kommando, Länge, Bytes
C0 02 E9 03
B0 10 00 0D 14 0D ...
11 00                     Sleep Out
   FF 78                  Wartemarke, 120 ms
29 00                     Display ON
3A 01 55                  <- unsere einzige Änderung (RGB565)
```

Ein Interpret dafür sind dreißig Zeilen. Der Gewinn ist unverhältnismäßig: **ein neues SPI- oder RGB-Panel braucht dann keinen C-Code mehr.** Code bleiben nur die drei *Arten* von Scanout — SPI-Stream, RGB-parallel über PIO, HSTX — und je Eingabechip ein Treiber. Alles andere ist Parameter.

---

# 10. Falsch sein dürfen

Ein verkehrtes Timing an einem parallelen RGB-Panel erzeugt keine Fehlermeldung, sondern ein schwarzes Bild. Drei Dinge gehören deshalb dazu:

- **die USB-Konsole als Rettungsleine** — funktioniert ohne jedes Display, und funktioniert heute schon
- **eine eingebaute Standardbeschreibung** plus Taste beim Booten, die den Satz ignoriert
- **die gerechneten Zahlen ausgeben**: DCLK, H/V-Totale, Framebufferadresse und -größe. Dann prüft man seine Beschreibung gegen das Datenblatt, ohne ein Oszilloskop zu besitzen.

---

# 11. Drei Schichten, nicht eine Datei

| Schicht     | Inhalt                                                    | Beispiel                         |
| ----------- | --------------------------------------------------------- | -------------------------------- |
| **Board**   | Pins und Busse: was ist wo verdrahtet                     | WeAct: PSRAM-CS 0; Waveshare: 47 |
| **Ausgabe** | Panelart, Timing, Pixelformat, Orientierung, Init-Sequenz | EP5008S, 480×854, RGB565, 30 Hz  |
| **Eingabe** | welche Zeigegeräte und Tastaturen an welchem Bus          | GT911 auf I²C, XPT2046 auf SPI0  |

Warum getrennt: **Fruit Jam ist ein festes Brett mit beliebigem Display, der WeAct ein beliebiges Brett mit festem Display.** Eine einzige Datei „so sieht meine Maschine aus" bekommt diese zwei Fälle nicht beide richtig.

---

# 12. Hardware im Blick

## 12.1 Als Nächstes: EP5008S auf WeAct RP2350B

- **WeAct RP2350B Core** — 41,4 × 41,1 mm, alle 48 GPIOs auf zwei 30-poligen Leisten, 16 MB QSPI-Flash, **PSRAM nur als unbestückter Footprint auf der Unterseite, CS auf GP0**. `pTOS/bios/Kconfig:479` kennt das Brett schon namentlich.
- **Erste Prüfung beim Auspacken:** ist das PSRAM bestückt? 480 × 854 × 2 = **800 KiB** passen nicht in 520 KB SRAM. Der Framebuffer *muss* nach 0x11000000. Die Bootmeldung sagt es (`initinfo` druckt PSRAM-Kennung oder „none").
- **Die Messung, die die Architektur trägt oder kippt: wie schnell zeichnet die VDI in PSRAM?** Scanout kostet ~0 % CPU, das stimmt — aber jeder Schreibzugriff geht über QSPI durch einen 8-KB-XIP-Cache, den PSRAM und Flash-Code teilen. Die Bringup-Notes führen die PSRAM-*Lese*bandbreite für den Scanout; die **Schreibseite der VDI steht nicht drin**. Messbar **heute, auf dem aktuellen Brett**: Alt-RAM läuft, 0x11000000 antwortet.
- **Haken in der Bringup-Reihenfolge:** Schritt 2 der Notes („Hersteller-Init unverändert, COLMOD 0x77, 24 Bit") ist mit 16 Datenleitungen **nicht durchführbar**. Der sichere Referenzpunkt, auf den sich die „immer nur eine Variable"-Disziplin stützt, existiert nicht — man startet zwangsläufig bei `3A = 55`.
- **Verdrahtungsdetail:** welche der acht Pins einer Farblane im 16-Bit-Modus die 5 bzw. 6 Bits tragen, muss aus dem ST7701S-Datenblatt kommen (üblicherweise die MSBs). Die unbenutzten LSBs gehören **auf GND, nicht offen** — offene LCD-Eingänge liefern halbe Pegel und damit Farbrauschen, das wie ein Timingfehler aussieht.
- **Touch GT911**, kapazitiv: I²C statt SPI, **absolute Koordinaten statt Rohwerte**, INT statt Polling, **keine Kalibrierung**, Multitouch. Der Treiberschnitt von gestern war richtig, aber zu früh fertig: Tap, Doppeltap, Hold und Tap-wird-Move stecken noch *im* XPT2046-Treiber. **Beim zweiten Zeigegerät muss das Gestenmodell eine Etage höher**, sonst schreibt man es zum zweiten Mal.

## 12.2 Auf dem Radar, nicht als Nächstes: Adafruit Fruit Jam

| Fruit Jam                       | Konsequenz                                                                         |
| ------------------------------- | ---------------------------------------------------------------------------------- |
| RP2350B, **8 MB PSRAM fest**    | der WeAct-Schritt ist kein Umweg — derselbe Chip, dieselbe Speicherfrage           |
| DVI über HSTX                   | dritter Displaypfad, zwingend auf GP12–19 (13.7.1)                                |
| **ESP32-C6 onboard**            | die AT-Strecke bleibt, aber an anderen UART-Pins → gehört in die Boardbeschreibung |
| **2-Port-USB-A-Hub onboard**    | bestätigt die PIO-USB-Richtung; dort ist der Hub kein Zubehör                      |
| SD per SPI **oder SDIO**        | unsere `sd.c` ist SPI — reicht, SDIO später                                        |
| TLV320DAC3100 per I²S           | DMAsound bekäme irgendwann echten Ton                                              |
| 5 NeoPixel, 3 Taster, STEMMA QT | erste echte Abnehmer der Ampel                                                     |

---

# 13. Oberfläche und Beschreibungssprache

## 13.1 Vier Ampelzustände, nicht drei

Ein frisches, unbeschriebenes Brett ist **nicht grün. Es ist unbekannt.** Grün heißt „frei, und ich weiß es"; unbekannt heißt „mir hat es niemand gesagt". Wer das verwechselt, sagt am ersten Tag grün über den Pin, an dem die SD-Karte hängt — und danach glaubt die Ampel keiner mehr.

```
[ GRAU ]  nicht beschrieben          <- ein neues Profil ist komplett grau
[ ROT  ]  belegt
[ GELB ]  frei, aber mit Bedingung
[GRUEN ]  frei, und vom Brett bestätigt   (res_claim hat nichts angemeldet)
```

Grau wird nur durch Beschreiben zu rot oder grün. Das macht die Arbeit auch sichtbar: das Brett nimmt Farbe an.

## 13.2 Zwei Arten von Anwendern

**Fertiges Brett** (Waveshare, Fruit Jam, ein Carrier): der Anwender *beschreibt nicht*, er *wählt*. Gelötet ist gelötet; PSRAM-CS, SD-Slot, Display-FPC sind entschieden. Für ihn ist die ganze Konfiguration eine Liste:

```
Welches Brett?
  1) Waveshare RP2350B-PiZero  +  ILI9341 320x240, resistiver Touch
  2) WeAct RP2350B             +  EP5008S 480x854, GT911
  3) Adafruit Fruit Jam        +  DVI über HSTX
  4) etwas anderes -- beschreiben wir es
```

Die Beschreibungsarbeit fällt **einmal pro Brett-Typ** an, bei dem, der es zuerst hat, und ist danach eine Datei, die alle anderen laden. Das Profil-Verzeichnis im Git ist der eigentliche Hebel, nicht die Oberfläche.

**Nacktes Brett** (z. B. ein RP2350B-Core-Board mit allen 48 Pins auf Leisten): hier verdrahtet der Anwender wirklich alles selbst, und Zuweisen ist echte Arbeit. Für ihn gibt es die Sprache aus 13.7.

## 13.3 Für neue Bretter: gerätezuerst, nicht pinzuerst

Ein Raster aus 48 Feldern lässt **den Anwender** das Nebenbedingungsproblem lösen. Eine Gerätefrage lässt es **das Werkzeug** lösen:

```
Wo sitzt die SD-Karte?
  SCK  : nur Pins, die spi sck können -> [2 6 10 14 18 22 26 30 34 38 42 46]
  Pin? 10
         ^ SPI1.  MOSI muss dann aus [11 15 27 31 43 47] kommen.
  MOSI? 11   MISO? 8   CS? 9
  -> SDCARD(SCK=GP10, MOSI=GP11, MISO=GP8, CS=GP9)      SPI1

Display?  2) ST7701S RGB565 parallel
  Braucht 16 fortlaufende Pins plus DCLK/HSYNC/VSYNC/DE.
  Erster Datenpin? 20
  -> GP20..GP35 = B0..B4 G0..G5 R0..R4 (LSB zuerst)
  i  GP12..GP19 bleiben frei -- die HSTX-Option lebt.
     Ab GP16 hätten Sie vier Pins mehr, aber HDMI wäre für dieses
     Brett für immer unmöglich.
```

Der letzte Hinweis ist der Grund, warum das Werkzeug existiert. Niemand sieht einem Pinraster an, dass er sich gerade HDMI verbaut hat.

## 13.4 Grafisch lesen, geführt schreiben

|                                   |                                                                                                                                                                    |
| --------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **Lesen: grafisch.**              | Eine 48-Kachel-Karte ist dem Listendruck wirklich überlegen — man sieht den roten RGB-Block auf einen Blick (`ROADMAP.md:1586`).                                   |
| **Schreiben: geführt, textuell.** | Pin-Drag-and-Drop ist aufwendig, schwer zu validieren und löst ein Problem, das der Anwender nicht hat: er zieht keine Leitungen, er liest ab, was er gelötet hat. |
| **Bretterbilder: nein.**          | Pro Brett Grafik zu pflegen veraltet sofort. „Das Board im Bild kann eigentlich weg. Nur die Chip-Anschlüsse sind interessant."                                    |

## 13.5 Technik

`tools/Connecty/` macht Python-Backend plus `workbench.html` — dieselbe Form: `gembios.py` serviert eine lokale Seite. Kein GUI-Toolkit, kein Paketieren, läuft überall, und **dieselbe Seite rendert die Ampel offline aus einer Profildatei**, ohne Brett, zum Planen vor dem Kauf.

1. **Die Datei ist die Wahrheit, nicht die Oberfläche.** Das Profil ist Text im Git; die Seite bearbeitet es. Dann kann man es im Notepad reparieren, diffen und verschicken. Die Oberfläche darf nie Daten besitzen, die in der Datei nicht stehen.
2. **Die Kommandozeile zuerst.** Die Seite ruft sie auf. Sonst entsteht eine zweite Implementierung, die anders rechnet.

## 13.5.1 Die drei Werkzeuge, und in welcher Reihenfolge

Sie stehen in `tools/` und beantworten drei verschiedene Fragen. Die Reihenfolge ist keine Empfehlung, sondern eine Abhängigkeit: jedes benutzt, was das vorige herausgefunden hat.

| | Werkzeug | Frage | Quelle |
| - | -------- | ----- | ------ |
| 1 | `svd2pins.py` → `rp2350_pins.csv` | **Was kann der Chip?** Welche Funktion liegt auf welchem FUNCSEL-Platz, wo sitzt der ADC, welche Pins können `xip_ss_n_1`. | `RP2350.svd` aus dem pico-sdk |
| 2 | `sch2net.py` | **Was hat der Brettbauer daraus gemacht?** Jeder GPIO, sein Netz, und was am anderen Ende hängt. | der Schaltplan des Bretts (Eagle `.sch`) |
| 3 | `gemprofile.py` | **Was haben wir entschieden, und was bleibt übrig?** Die Ampel, die Kollisionen, die Bandbreite, der Takt. | die `.gem`-Profildatei |

Der mittlere Schritt kam zuletzt dazu und war der, der gefehlt hat. Ein gekauftes Brett beschreibt sich dreimal — Pinout-Seite, Boarddefinition des Herstellers, Schaltplan — und nur das letzte ist eine Tatsache. Die ersten beiden sind Zusammenfassungen, und eine Zusammenfassung lässt weg, wofür ihr Verfasser keine Verwendung hatte. Zuverlässig ist das genau der Teil, der einen Tag kostet.

Beim Adafruit Fruit Jam, am 10.10.2026 gelesen, standen drei Dinge im Schaltplan, die in keiner der beiden anderen Quellen vorkamen:

- **GP1/GP2 gehen nicht an eine Buchse**, sondern an den *Upstream*-Port eines CH334F-Hubs; die beiden USB-A-Buchsen hängen an dessen Downstream-Ports. Ein Host-Controller, zwei Geräte — Tastatur **und** Maus, nicht entweder/oder. Das ist ein anderer Treiber und ein anderer Plan.
- **GP46 ist zugleich ESP-Chipselect und `!OE` eines 74AHC1G125**, eines einzelnen Tri-State-Puffers, der den MISO des ESP nur dann auf die geteilte Leitung legt, wenn der ESP ausgewählt ist. Das Brett schlichtet seinen SPI-Bus **in Hardware** — genau das, was unsere Ampel sonst nur als Vorbehalt vermerken kann (13.1). Ohne diesen Befund würde man den Bus entweder meiden oder sich mit ihm prügeln.
- **GP22 legt Audio-Codec und ESP32-C6 gemeinsam zurück**, und **GP23** ist Codec-Interrupt *und* Bootstrap-Pin des ESP *und* ein Lötfeld. Drei Aufgaben auf zwei Pins, keine davon aus einer Pintabelle zu sehen. Nebenbei folgt daraus, dass das Brett sein eigenes Funkteil neu flashen kann, ohne dass ein Kabel dazukommt.

Daraus der Merksatz: **den Schaltplan lesen, jedes Mal, bevor ein Profil geschrieben wird.** `sch2net.py` macht das billig — eine Eagle-`.sch` ist XML, es braucht keine Bibliothek, und die ganze Arbeit besteht darin, die Netzliste umzustülpen, damit sie nach Pin sortiert ist statt nach Netz.

`sch2net.py` urteilt dabei nicht, es berichtet. Ob ein Pin benutzt werden *darf* und von wem, ist die Frage von `gemprofile.py`, und die Antwort steht in einer `.gem`-Datei, die ein Mensch schreibt, nachdem er das hier gelesen hat.

## 13.6 Stufen

| Stufe | Was                                         | Für wen                                           |
| ----- | ------------------------------------------- | ------------------------------------------------- |
| 1     | Profil-Liste + `gembios.py --show` als Text | alle mit bekanntem Brett — **das sind fast alle** |
| 2     | geführter Dialog, gerätezuerst, mit Prüfung | wer ein neues Brett beschreibt                    |
| 3     | HTML-Ansicht der Ampel, nur lesen           | wer sie beim Basteln offen stehen lässt           |
| 4     | Bearbeiten im Browser                       | **nur, wenn Stufe 2 wirklich weh tut**            |

Stufe 4 bewusst *nicht* planen. Sie ist das, was man baut, wenn sich jemand über Stufe 2 beschwert — und bis dahin ist offen, worüber genau.

## 13.7 Die Beschreibungssprache

Form:

```
BOARD  "bare-rp2350b"  CHIP=RP2350B  FLASH=16M

DEFINE PSRAM   (CS=GP47);
DEFINE COM     (#0, TX=GP0,  RX=GP1);
DEFINE COM     (#1, TX=GP4,  RX=GP5);
DEFINE SDCARD  (#0, SCK=GP10, MOSI=GP11, MISO=GP8, CS=GP9);      -- SPI1

DEFINE DISPLAY (#0, TYPE=RGBPANEL, CHIP=ST7701S, PANEL="ep5008s",
                    RES=480x854, FORMAT=RGB565, REFRESH=30,
                    CFG=S3W(_CS=GP40, SCL=GP41, SDA=GP42),
                    BASE=GP20,            -- B0..B4 G0..G5 R0..R4, LSB zuerst
                    DCLK=GP36, HSYNC=GP37, VSYNC=GP38, DE=GP39);
DEFINE TOUCH   (SDA=GP44, SCL=GP45, INT=GP43, RESET=GP46);   -- I2C0, GT911

RESERVE GP2, GP3  "WS2812";
RESERVE GP6, GP7  "Drehgeber";
```

Dieses Beispiel ist gegen die SVD-Tabelle geprüft: keine ungültige Zuordnung, keine Doppelbelegung. Übrig bleiben `GP12..GP19` — der komplette HSTX-Block, unangetastet — und vier Pins für eigene Sachen.

Und eine unbequeme Nebenrechnung: `RESET` und `BL` des Panels brauchen zwei **weitere** Pins. Die vier freien sind hier schon für WS2812 und Drehgeber reserviert — also entweder die NeoPixel, oder der Drehgeber, oder die HSTX-Option. **Ein nacktes 48-Pin-Brett mit 16-Bit-Panel ist knapp, und das sieht man ihm nicht an.** Genau dafür zählt das Werkzeug mit.

### Das DISPLAY-Statement

Ein Schlüsselwort mit `TYPE=`, nicht je Art ein eigenes: ein Slot in der Ressourcentabelle, eine Parserregel, und ein neuer Typ bringt kein neues Schlüsselwort mit.

`CHIP=` und `PANEL=` sind **zwei Dinge**: `CHIP` sagt, welche Registersprache gesprochen wird (ein ST7701S fährt viele verschiedene Gläser), `PANEL` nennt das konkrete Glas und damit die Init-Sequenz aus dem Katalog. Fehlt `PANEL`, muss `INIT="…"` da sein; fehlt `CHIP`, kann das Werkzeug eine fremde Sequenz nicht einmal auf Plausibilität prüfen.

`CFG=` nennt den **seriellen Konfig-Kanal** als eigene Art samt Pins, statt die Leitungen einzeln hinzustreuen:

```
CFG = S3W (_CS=.., SCL=.., SDA=..)      -- 3-Wire, 9 Bit mit D/C-Bit.
                                           Was der EP5008S-Testsatz "S3W" nennt.
CFG = SPI4(_CS=.., SCL=.., SDA=.., DC=..)
CFG = I2C (SDA=.., SCL=.., ADDR=0x..)
CFG = NONE                               -- Panel braucht keinen Init (HSTX)
```

Der Gewinn: der Prüfer weiß damit, dass diese Leitungen **nur beim Booten** gebraucht werden. Werden `SCL`/`SDA` danach zum System-I²C, ist das keine Kollision, sondern eine Übergabe, und nur ein Kanal mit Namen kann das ausdrücken. Bei `TYPE=SPI` fallen Konfig-Kanal und Datenweg zusammen: dieselbe Leitung.

Was die drei Typen brauchen:

```
TYPE=SPI        CHIP PANEL RES FORMAT + SCK MOSI CS DC RST LED     kein Timing
TYPE=RGBPANEL   alles oben + CFG + BASE/DCLK/HSYNC/VSYNC/DE        Timing nötig
TYPE=HSTX       RES REFRESH FORMAT + die vier Paare                CFG=NONE
```

**HSTX-Pins werden ausgeschrieben, obwohl sie fix sind** — denn fix ist nur die *Menge*. Der Block treibt acht Bits auf GP12–19; welches Paar am Stecker Takt, D0, D1 oder D2 wird, entscheidet die Leiterbahnführung des Bretts, und darin unterscheidet sich ein Fruit Jam von einem DVI-Breakout:

```
DEFINE DISPLAY(#0, TYPE=HSTX, RES=640x480, REFRESH=60, FORMAT=RGB565,
                   CLK = GP12/GP13,        -- je Paar: plus/minus
                   D0  = GP14/GP15,
                   D1  = GP16/GP17,
                   D2  = GP18/GP19,
                   CFG = NONE);
```

Der Prüfer kann damit etwas tun: acht Pins vollständig, jedes Paar benachbart, keiner doppelt — und ein „sicher?", wenn die Paarreihenfolge von der üblichen abweicht. Und in der Ampel stehen dann acht rote Kacheln mit „HSTX D1−" statt achtmal „HSTX".

**Kein `ROTATE`, kein `MIRROR`.** Bei einem gestreamten Panel kann der DMA eine Zeile nicht rückwärts lesen; 90°/270° bräuchten einen transponierenden Blit bei jedem Zeichnen. Und es braucht auch kein Feld: Spiegelung ist Sache des Controllers und steht schon in der Init-Sequenz (`36 = 08` beim EP5008S, MADCTL). **Als Daten bereits ausdrückbar.**

**`#0` ist der GEM-Bildschirm, und GEM hat einen.** Ein `#1` wäre kein zweiter Bildschirm, sondern ein Gerät — technisch möglich (SPI-Panel und HSTX gleichzeitig), aber nichts, was die VDI kennt. Zunächst ablehnen, mit genau dieser Begründung, statt es stillschweigend zuzulassen.

### 13.7.1 Panel und HDMI teilen sich GP12–19

Zwei Befunde, die zusammengehören:

```
hstx_0 .. hstx_7   gibt es ausschliesslich auf GP12 .. GP19     (SVD)
#0                 ist der GEM-Bildschirm, und GEM hat einen    (oben)
```

Der erste sagt: diese acht Pins sind die **einzige** Stelle, an der dieses Brett
je HDMI können wird. Der zweite sagt: zwei Bildschirme kann es ohnehin nicht
geben. Daraus folgt, dass eine Überlappung **nichts kostet** — und damit die
Belegung, die man sonst nicht wählen würde:

```
RGBPANEL   GP12..GP16 = R0..R4,  GP17 = DCLK,  GP18 = HSYNC,  GP19 = VSYNC
HSTX       GP12/13 = CLK±,  GP14/15 = D0±,  GP16/17 = D1±,  GP18/19 = D2±
```

Dieselben acht Leitungen, dieselben acht Löcher, und das Brett behält beide
Möglichkeiten. Wer die Panelleitungen stattdessen irgendwo anders hinlegt,
bezahlt dafür nichts — und verliert HDMI für immer. Es ist die seltene Sorte
Entscheidung, bei der die bessere auch die billigere ist.

**Und es braucht dafür kein neues Konstrukt.** Zwei `DEFINE DISPLAY(#0, …)` in
einer Datei verbietet die Regel oben zu Recht, und ein `IF` verbietet sich
selbst (siehe *Zwei Festlegungen zur Semantik*: sobald die Sprache rechnen kann,
will jemand den Interpreten im BIOS haben). Also sind es **zwei Profile**:

```
profiles/<brett>-panel.gem      DISPLAY #0  TYPE=RGBPANEL   GP12..19 = R0..R4/DCLK/HSYNC/VSYNC
profiles/<brett>-hdmi.gem       DISPLAY #0  TYPE=HSTX       GP12..19 = die vier Paare
```

Die Ampel färbt in beiden dieselben acht Kacheln rot, nur mit anderen Namen
darin. Umgeschaltet wird nicht durch einen Mechanismus, sondern durch eine
`gembios.py`-Sitzung: TRY, Bild da, SAVE. Eine Erkennungsleitung, die das Brett
selbst ausliest, braucht es nicht — wer umsteckt, weiß es.

**Die Mehrdeutigkeit lebt in der Dateiauswahl, nicht in der Syntax.**

Eine Einschränkung, die erst mit einer Platine beißt: eine DVI-Buchse bringt
Abschluss- und ESD-Bauteile mit, und die sitzen dann auf Leitungen, die im
Panelbetrieb das Display treiben. Nicht die TMDS-Paare sind das Problem,
sondern was man ihnen zuliebe an die Leitung hängt. Fliegend verdrahtet ist
das gegenstandslos.


### Was `RES=` still mitentscheidet

`RES` ist das Feld, das darüber entscheidet, ob die Maschine überhaupt läuft — und das sagt es nicht von selbst. Der Prüfer rechnet darum mit:

```
480x854   RGB565  FB  819.840 B (0,78 MiB)   HTOT 528  VTOT 897
   30,0 Hz -> DCLK 14,208 MHz   PSRAM-Read 24,60 MB/s
   55,5 Hz -> DCLK 26,286 MHz   PSRAM-Read 45,50 MB/s   (Herstellerbetrieb)

1024x720  RGB565  FB 1.474.560 B (1,41 MiB)  HTOT 1072 VTOT 763
   30 Hz   -> DCLK 24,538 MHz   PSRAM-Read 44,24 MB/s
   60 Hz   -> DCLK 49,076 MHz   PSRAM-Read 88,47 MB/s
```

1024×720 bei 30 Hz ist das **1,8-fache** des EP5008S — auf demselben QMI-Bus, den das Flash für den XIP-Code mitbenutzt. Nicht ausgeschlossen, aber zu messen. Und die 1,41 MiB sind der zweite Grund, warum PSRAM beim WeAct Pflicht ist und nicht bloß schön wäre.

### Neun Festlegungen

1. **Signalnamen kommen vom Chip.** `TX` ist unser Wort, `uart0_tx` das des Chips. Bildet die Geräteklasse ihre Signale auf SVD-Namen ab, ist der Prüfer ein Nachschlagen und keine Tabelle, die wir pflegen.
2. **`#n` wird abgeleitet, nicht angegeben.** Aus `TX=GP0` folgt zwingend UART0. Angegeben wirkt `#n` als *Behauptung*, die geprüft wird — nicht als Zuweisung, bei der man sich irren kann.
3. **Bereiche brauchen eine Richtung.** `R=GP20..GP26` sagt nicht, welches Ende Bit 0 ist; für `out pins, 16` entscheidet das alles. Normalfall `BASE=GP20`, und der Prüfer *druckt* die 16 Leitungen aus. Explizite Listen nur für krumme Verdrahtungen.
4. **`RESERVE` für das, was der Anwender selbst nimmt.** Sonst wird grau nie zu grün. Das ist der vierte Eingang neben Chip, Brett und Treiber.
5. **Ein Kopf ist nötig.** Ohne `CHIP=` kann der Prüfer nicht wissen, dass GP48 nicht existiert — und auf einem RP2350A wäre schon GP30 zu viel.
6. **PIO sieht nur ein Fenster von 32 Pins** (Basis 0 oder 16). Alle Datenleitungen *und* DCLK müssen in dasselbe Fenster fallen. **Vor dem Einbau als Regel im Datenblatt nachschlagen.**
7. **Kommentare und freie Zeilenumbrüche.** Billig, und ohne sie wird die Datei nicht gepflegt.
8. **`FORMAT=` ist nicht optional.** Es legt die Leitungszahl fest (RGB565 = 16, RGB666 = 18, RGB888 = 24). Ohne das Feld sind „17 Datenleitungen" kein Fehler, sondern nur eine Zahl.
9. **Belegung wird genannt, nie hergeleitet.** Ist ein Pin weg, steht er in der Datei — auch wenn die Hardware keine Wahl hatte (HSTX). Sonst erklären Datei und Ampel denselben roten Pin verschieden, und die Datei ist nicht mehr die einzige Quelle.

### Zwei Festlegungen zur Semantik

**Deklarativ, keine Befehlsfolge.** `DEFINE` sieht nach Ausführung aus, ist aber eine Aussage über die Welt. Reihenfolge egal, Dateien zusammenmischbar, und der Prüfer meldet **alle** Konflikte auf einmal statt beim ersten abzubrechen.

**Das BIOS liest diesen Text nie.** Der PC übersetzt ihn in den Binärsatz; im Flash liegen Bytes. Damit bleibt im BIOS kein Parser, keine Fehlertexte, keine Syntaxversion — und die Sprache darf sich weiterentwickeln, ohne die Firmware anzufassen.

Daraus die wichtigste Beschränkung: **kein `IF`, keine Arithmetik, keine Verschachtelung.** Eine Zeile je Aussage, eine Regex je Anweisung. Sobald die Sprache rechnen kann, ist sie eine Programmiersprache und braucht einen Interpreten — und jemand wird ihn im BIOS haben wollen.

## 13.8 Warum sich das lohnt: ein echter Durchlauf

Ein erster Entwurf von Hand, durch die SVD-Tabelle geschickt:

```
DEFINE COM(#0, TX=GP3, RX=GP4);
  ! GP3 kann kein UART-TX.  An UART kann GP3 nur: uart0_rts, uart0_rx
  ! GP4 kann kein UART-RX.  An UART kann GP4 nur: uart1_tx
  -> TX und RX vertauscht, und auf zwei verschiedenen UARTs.
     uart0 bildet Paare 0/1, 2/3, 12/13 ...   uart1 bildet 4/5, 6/7, 8/9 ...
     GP3/GP4 liegt genau auf der Nahtstelle.   Meinten Sie TX=GP2, RX=GP3?

DEFINE COM(#1, TX=GP7, RX=GP8);
  ! ebenfalls vertauscht: GP7 kann nur uart1_rx, GP8 nur uart1_tx
                                               Meinten Sie TX=GP8, RX=GP9?

DEFINE DISPLAY(#0, RES=1024x720, TYPE=RGBPANEL,
               R=GP20..GP25, G=GP26..GP31, B=GP32..GP36, ...);
  ! R = 6 Leitungen, G = 6, B = 5  ->  17 Datenleitungen
    Kein Format hat 17:  RGB565 = 16,  RGB666 = 18,  RGB888 = 24
    FORMAT= fehlt, sonst hätte ich das gleich sagen können.
  ! TIMING fehlt.  Bei TYPE=RGBPANEL ist RES= nur die aktive Fläche;
    ohne Porches und DCLK lässt sich das PIO nicht programmieren.
  i 1024x720 RGB565 -> 1,41 MiB Framebuffer, bei 30 Hz 44,24 MB/s
    PSRAM-Read -- das 1,8-fache des EP5008S.  Zu messen.

DEFINE TOUCH(SDA=GP46, SCL=GP47, INT=GP48);
  ok  GP46 = i2c1_sda, GP47 = i2c1_scl
  !   GP48 existiert nicht.  Der RP2350B hat GP0..GP47.
  i   GP47 ist einer von nur vier Pins, die den PSRAM-CS tragen können
      (0, 8, 19, 47).
```

Vier Fehler in vier Zeilen — und **keiner davon wäre am Oszilloskop als solcher erkennbar.** Vertauschtes TX/RX sieht aus wie ein kaputtes Modul; 19 statt 16 Datenleitungen sieht aus wie ein Timingproblem. Die Fehlermeldungen sind das Produkt, nicht die Syntax.

---

# 14. Offene Entscheidungen

1. **Satzformat A oder B** (5.3) — die einzige, die beide Seiten gleichzeitig festlegt. Empfehlung: B.
2. Wartezeit-Standard: 5 s für Tischgerät, 0 für eingebautes — wer entscheidet das, die Beschreibung oder ein Schalter?
3. Reicht ein Satz von 4 KB? (Pins + Timing: reichlich. Eine eingefügte Custom-Init-Sequenz: knapp, ~400 Bytes.)
4. Wo wohnt das Gestenmodell, wenn es aus `rp2350_touch.c` herauskommt?
5. Framebuffer in PSRAM — hält die VDI das aus? (Siehe 12.1, und es ist heute messbar.)
6. PIO-Fenster: Basis 0 oder 16, also 32 sichtbare Pins — im Datenblatt bestätigen, bevor es als Prüfregel einzieht (13.7, Punkt 6).
7. Wo wohnt das Profil-Verzeichnis, aus dem Stufe 1 ihre Liste nimmt? (Im Repo, oder nachladbar?)

---

# 15. Board-IO und User-Mode

Zwei Welten, die strikt getrennt bleiben müssen:

- **Board-IO** — was GEMbedded selbst anspricht: Display, Touch, SD, UART, PSRAM.
- **User-Mode** — was der Anwender anschließt: Sensoren, Waagen, Schrittmotortreiber, NeoPixel, OneWire, Drehgeber.

Der Anwender muss **glasklar sehen, was nicht geht** — und zwar bevor er Hardware anschließt, nicht danach.

## 15.1 Was die Konfiguration kann, und was ein Neuübersetzen braucht

|                          |                                                                                                                           |
| ------------------------ | ------------------------------------------------------------------------------------------------------------------------- |
| **Konfiguration reicht** | Pinnummern, Auflösung, Porches, Pixelformat, Orientierung, Init-Sequenz, welche Eingabegeräte, PSRAM-CS, Framebuffergröße |
| **Neu übersetzen**       | welche *Treiberarten* im Image sind; Linkerskript; die SRAM-Teilung mit rtcore                                            |

Daraus die Regel: **ein Image enthält alle Treiber, die Konfiguration wählt und parametriert sie aus.** Bei 16 MB Flash ist das geschenkt. Neu übersetzen muss dann nur, wer eine *neue Art* Gerät hinzufügt — also wir, nicht der Anwender.

## 15.2 Befund: die Speicherseite ist längst dynamisch

„Ein anderes SPI-Display mit mehr oder weniger Pixeln anklemmen und nur die Konfiguration neu machen" ist näher als es aussieht:

```c
/* pTOS/bios/screen.c:685 */
vram_size    = initial_vram_size();
screen_start = balloc_stram(vram_size, TRUE);
```

`initial_vram_size()` ist eine **Funktion**, und auf dem Falcon rechnet sie die Größe schon aus dem Videomodus (`vgetsize(vsetmode(-1))`). Nur der RP2350-Zweig gibt eine Konstante zurück:

```c
/* pTOS/bios/screen.c:811 */
return RP2350_LCD_VRAM_SIZE + 2048UL;
```

Der ganze Rest — `balloc_stram`, `v_bas_ad`, `video_ram_size`, `Srealloc` — ist bereits auflösungsunabhängig. Und `rp2350_touch.c` ist es seit dem Treiberschnitt ebenfalls (`scr_w`/`scr_h` aus `screen_get_current_mode_desc()`).

## 15.3 Die neun einbetonierten Zeilen

Vollständige Liste dessen, was beim SPI-Display heute noch zur Bauzeit festliegt:

```c
rp2350_lcd.c:42   #define LCD_SCK   10
rp2350_lcd.c:43   #define LCD_MOSI  11
rp2350_lcd.c:44   #define LCD_CS    24
rp2350_lcd.c:45   #define LCD_RST   23
rp2350_lcd.c:46   #define LCD_DC    25
rp2350_lcd.c:47   #define LCD_LED   21
rp2350_lcd.c:93   #define WIDTH     320
rp2350_lcd.c:94   #define HEIGHT    240
rp2350_lcd.h:20   #define RP2350_LCD_VRAM_SIZE  (320UL * 240UL * 2)
```

Das ist der ganze Abstand zwischen heute und „Display tauschen, Konfiguration neu, fertig". **Hier wird die Idee zur Aufgabe.**

## 15.4 Warum „fix" keinen Grund hat

Bequemlichkeit, nicht Notwendigkeit. Eine `#define`-Pinnummer wird ein Direktoperand, eine Variable ein Ladevorgang — aber Pinnummern werden **einmal beim Init** benutzt, nicht pro Pixel. Selbst beim PIO: das Programm wird zur Bauzeit assembliert, die **Pinbasis steht in `PINCTRL`, einem Laufzeitregister**. Es kostet also nichts.

## 15.5 Die Trennung steht schon in der Syntax

```
DEFINE ...     <- das System beansprucht.   Board-IO.
RESERVE ...    <- der Anwender beansprucht. User-Mode.
```

Zwei Schlüsselwörter, eine Grenze — und beide landen in **derselben** Registratur, sonst weiß keine Seite von der anderen.

## 15.6 Nicht wrappen: die fünf Türen des Cores

Bibliotheken zu wrappen ist ein Spiel, das man verliert — es gibt Tausende, sie werden aktualisiert, und sie rufen sich gegenseitig auf. Aber **alle** gehen durch dieselben Türen des Arduino-Cores:

```
pinMode()          SPI.begin()        Wire.begin()
attachInterrupt()  pio_claim_*() / dma_claim_*()
```

Fünf Stellen, und sie decken jede Bibliothek automatisch ab — auch die von nächstem Jahr. Steht so schon in `ROADMAP.md:408`.

## 15.7 Zuweisen statt prüfen — und warum beides nötig ist

Richtig zuweisen ist besser als hinterher prüfen. Aber die ROADMAP will auch, dass **alte Sketches transportabel bleiben** (`:300`). Das geht zusammen, wenn beides durch eine Tür läuft:

```c
/* alter Sketch: Pin steht fest -> wird geprüft */
pinMode(17, OUTPUT);
        -> abgelehnt, mit Begründung, vor dem Ende von setup()

/* neuer Sketch: fragt statt zu nennen -> Kollision unmöglich */
int pin = gem_claim_pin(GEM_NEED_PIO, "WS2812");
```

Der zweite Weg ist der richtige, der erste der nötige. Dieselbe Registratur beantwortet beide.

## 15.8 Es sind nicht nur Pins

Was ein Anwenderprogramm **unsichtbar** stehlen kann:

- einen **DMA-Kanal** (von 16)
- eine **PIO-State-Machine** — und schlimmer: **Instruktionsplätze** (32 je Block)
- **Core 1**, der rtcore gehört
- **Zeit und Bandbreite**: eine enge Schleife auf Core 0 hungert GEM aus, und der PSRAM-Bus wird mit dem Scanout geteilt

Für die ersten drei ist `gem_claim_*` die Tür. Beim vierten kann die Ampel nur warnen — was schon viel ist, weil niemand damit rechnet.

## 15.9 Wie „glasklar" aussieht

```
GEMbedded: dieser Sketch will GP17.
           Belegt vom Display (EP5008S DCLK).
           Frei und PIO-fähig: GP2, GP3, GP6, GP7.
           Abgebrochen, bevor setup() fertig war -- nichts wurde angefasst.
```

**Abbruch vor dem Ende von `setup()`** ist der Punkt: danach laufen schon Interrupts auf fremden Pins, und dann ist Hardware beschädigt, bevor die Meldung kommt (`ROADMAP.md:379`).

---

# 16. Merksätze

1. **Die Startreihenfolge bestimmt, wo eine Beschreibung liegen darf** — nicht der Geschmack.
2. **Der Konfigspeicher ist schon gebaut** (`rp2350_nvram.c`), absichtlich leer, und sein Header nennt „which pins the system has spoken for" selbst als Kandidaten.
3. **Jede Flash-Schreiboperation kostet 23 ms ohne PSRAM.** Darum wird vor dem Displaystart gespeichert.
4. **TRY heilt sich selbst** — eine schlechte Probekonfiguration ist nach dem nächsten Einschalten weg.
5. **Init-Sequenzen sind Daten.** Dann braucht ein fremdes Panel keinen Compiler.
6. **Der Registrar muss mit dem ersten Treiber anfangen**, nicht mit dem dreißigsten.
7. **PIO-Instruktionsspeicher ist die Ressource, die zuerst ausgeht** — nicht die State Machines.
8. **Gelb ist die wertvollste Farbe.** Rot/grün ist nur Subtraktion.
9. **Die Chip-Tabelle wird generiert, nicht abgetippt** — und vergisst darum HSTX nicht.
10. **Die Differenz zweier echter Boardprofile ist die Beschreibung.** Abgelesen statt erfunden.
11. **Ein unbeschriebenes Brett ist grau, nicht grün.** Grün muss verdient werden.
12. **Grafisch lesen, geführt schreiben.** Niemand zieht Leitungen in einer Oberfläche — er liest ab, was er gelötet hat.
13. **Die Datei ist die Wahrheit, nicht die Oberfläche.**
14. **Das BIOS liest keinen Text.** Der PC übersetzt, im Flash liegen Bytes — darum darf die Sprache wachsen, ohne die Firmware anzufassen.
15. **Die Fehlermeldungen sind das Produkt**, nicht die Syntax. Vertauschtes TX/RX sieht am Oszilloskop aus wie ein kaputtes Modul.
16. **Ein Image enthält alle Treiber**; die Konfiguration wählt aus. Neu übersetzen nur für eine neue *Art* Gerät.
17. **Die Speicherseite ist schon dynamisch** — `initial_vram_size()` ist eine Funktion. Es fehlen neun `#define`-Zeilen (15.3).
18. **„Fix" hatte nie einen Grund.** Pinnummern werden einmal beim Init benutzt, und die PIO-Pinbasis steht in einem Laufzeitregister.
19. **`DEFINE` ist das System, `RESERVE` der Anwender** — die Trennung von Board-IO und User-Mode steht in der Syntax.
20. **Nicht Bibliotheken wrappen, sondern die fünf Türen des Cores schließen.**
21. **Abbrechen, bevor `setup()` fertig ist.** Danach ist die Hardware schon angefasst.
22. **`RES=` entscheidet still über alles** — Framebuffergröße, DCLK, PSRAM-Bandbreite. Der Prüfer rechnet es vor, der Anwender tippt es nur.
23. **Belegung wird genannt, nie hergeleitet** — auch wo die Hardware keine Wahl hatte.
24. **Spiegeln braucht kein Feld.** Es steht als MADCTL schon in der Init-Sequenz. Rotieren geht bei gestreamten Panels gar nicht billig.
25. **Vier Dateiarten machen das Ökosystem** (1.2), und zwei davon kann ein Fremder schreiben.
26. **GP12–19 gehören dem Bildschirm — welcher auch immer es ist.** Sie sind die einzigen HSTX-Pins, und GEM hat nur einen Bildschirm; also kostet die Doppelnutzung nichts, und jede andere Belegung verkauft HDMI für immer (13.7.1).
27. **Zwei Betriebsarten sind zwei Dateien, kein `IF`.** Die Mehrdeutigkeit lebt in der Dateiauswahl, nicht in der Syntax — sonst zieht ein Interpret ins BIOS ein.
28. **Jede Zahl hat eine Herkunftsklasse** — Chip, Treiber, Datenblatt, Entwurf. Nach den ersten drei darf man verdrahten, nach der vierten nicht (Kopf).
29. **Den Schaltplan lesen, jedes Mal.** Ein gekauftes Brett beschreibt sich dreimal, und nur die Netzliste ist eine Tatsache — die anderen zwei sind Zusammenfassungen, und weggelassen wird zuverlässig das, was einen Tag kostet (13.5.1).
30. **Bei HSTX ist der Pixeltakt keine freie Größe.** `CLK_HSTX_DIV` teilt nur ganzzahlig, also ist er eine Teilung der PLL, die ohnehin läuft. Der Hebel heißt dann nicht Bildrate, sondern **Auflösung** — und der breitere Modus kann der billigere sein.
