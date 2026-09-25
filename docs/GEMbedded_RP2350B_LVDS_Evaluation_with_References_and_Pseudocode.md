# GEMbedded / RP2350B – Direkter LVDS-Scanout über HSTX

**Design-Notiz aus der Evaluationssession**  
**Datum:** 24.09.2026

## 1. Ziel

Untersucht wird der direkte Anschluss eines kleinen LVDS-Displays an einen **RP2350B**, möglichst ohne zusätzliche aktive Display-Hardware.

Das System ist ausdrücklich **keine Multimedia-Plattform**. Zielanwendungen sind Embedded-Bedienoberflächen und Dashboards, z. B.:

- Maschinenbedienpanel
- Home-Automation-Dashboard
- Akku-Gerätepanel
- Waschmaschine / Haushaltsgerät
- BHKW- oder Heizungssteuerung
- Mini-Robotersteuerung
- 3D-Drucker
- Staubsauger
- mobiles Messgerät
- Drucker
- Klimagerätesteuerung
- CNC-/Industrie-Bedienung

Die Prioritäten sind daher:

1. **geringe CPU-Last**
2. **Core 1 vollständig für Echtzeit-/Anwendungsaufgaben freihalten**
3. **geringe PSRAM-Bandbreite**
4. **kleiner Framebuffer**
5. **schneller Systemstart**
6. **deterministisches Verhalten**
7. **keine externe aktive Display-Bridge / kein FPGA / kein LVDS-Serializer**
8. Farbtiefe ist zweitrangig

---

## 2. CPU-Aufteilung

### Core 0

Core 0 soll vollständig für die grafische Umgebung zuständig sein:

- pTOS
- GEM
- GEMbedded
- Applikationen
- Zeichnen in den Framebuffer
- GUI-Logik

### Core 1

Core 1 soll **nicht** für den Display-Scanout verwendet werden.

Er bleibt frei für Aufgaben wie:

- Motorregelung
- Sensorik
- Messwerterfassung
- Echtzeitkommunikation
- CAN / UART / SPI
- Robotik
- Klipper / 3D-Druck
- Encoder
- Regelalgorithmen
- sonstige zeitkritische Prozesse

Der laufende Display-Refresh soll daher idealerweise vollständig durch **DMA + PIO + HSTX** erfolgen.

---

## 3. Ursprünglich betrachtete Farbformate

### RGB565

16 Bit pro Pixel.

Framebuffer:

- 800 × 480: `800 × 480 × 2 = 768.000 Byte`
- 800 × 600: `800 × 600 × 2 = 960.000 Byte`

Vorteil:

- gute Farbdarstellung

Nachteile:

- hohe PSRAM-Bandbreite
- LVDS-Bitpacking aus RGB565 ohne CPU relativ aufwendig
- nicht optimal für einen vollständig autonomen PIO/HSTX-Scanout

RGB565 wurde deshalb für dieses Projekt verworfen.

### RGB332

8 Bit pro Pixel.

Framebuffer:

- 800 × 480: 384 kB
- 800 × 600: 480 kB

Vorteil:

- deutlich geringere Bandbreite als RGB565
- 256 Farben

Nachteil:

- direkte Umsetzung in die drei 7-Bit-LVDS-Datenströme ist weiterhin nicht trivial
- benötigt zusätzliche Bit-Umsortierung

RGB332 bleibt theoretisch möglich, ist aber für das Ziel **0 % Core-Last beim Scanout** nicht die eleganteste Lösung.

### 8-Bit Indexed Color

Vorteil:

- 256 frei definierbare Farben

Nachteile:

- Palette bzw. LUT muss während des Scanouts ausgewertet werden
- zusätzliche Verarbeitung zwischen Framebuffer und HSTX

Für die gewünschte minimalistische Hardware-Pipeline daher nicht bevorzugt.

### 4-Bit planar

Framebuffer:

- 800 × 600: 240 kB

Vorteil:

- sehr geringe Bandbreite
- historisch gut zu GEM passend

Nachteil:

- mehrere Bitplanes müssen für den LVDS-Stream wieder zusammengeführt werden
- für den Scanout komplizierter als ein sinnvoll gewähltes chunky 4-bpp-Format

---

## 4. Aktuelle Entscheidung: 4-bpp RGBI

**4-bpp RGBI bedeutet insgesamt vier Bit pro Pixel.**

Die vier Bits sind:

- R = Rot
- G = Grün
- B = Blau
- I = Intensity

Damit stehen

`2^4 = 16 Farben`

zur Verfügung.

Wichtig:

**Es sind nicht 4 Bit Farbe plus Intensity, sondern insgesamt 4 Bit inklusive Intensity.**

Beispiel:

```text
R G B I
0 0 0 0  -> Schwarz
1 0 0 0  -> dunkles Rot
1 0 0 1  -> helles Rot
0 1 0 0  -> dunkles Grün
0 1 0 1  -> helles Grün
1 1 1 0  -> helles Grau
1 1 1 1  -> Weiß
```

Für GEMbedded ist die begrenzte Farbtiefe akzeptabel.

Das System soll kein Foto-, Video- oder Gaming-System sein. Für typische Embedded-UIs werden primär benötigt:

- Hintergrundfarben
- Text
- Buttons
- Rahmen
- Statusanzeigen
- Diagramme
- Warnfarben
- Alarmfarben
- OK-/Fehlerzustände

Hier reichen 16 sinnvoll definierte Farben in vielen Anwendungen aus.

---

## 5. Speicherbedarf

### 800 × 480, 4 bpp

```text
800 × 480 × 0,5 Byte
= 192.000 Byte
```

Framebuffergröße:

**192 kB**

### 800 × 600, 4 bpp

```text
800 × 600 × 0,5 Byte
= 240.000 Byte
```

Framebuffergröße:

**240 kB**

Damit ist selbst Double Buffering in der vorhandenen 16-MB-PSRAM problemlos möglich.

---

## 6. PSRAM-Bandbreite

### 800 × 600 bei 30 Hz

```text
240.000 Byte × 30
= 7.200.000 Byte/s
```

also:

**7,2 MB/s**

### 800 × 600 bei 20 Hz

```text
240.000 Byte × 20
= 4.800.000 Byte/s
```

also:

**4,8 MB/s**

### 800 × 480 bei 30 Hz

```text
192.000 × 30
= 5,76 MB/s
```

### 800 × 480 bei 20 Hz

```text
192.000 × 20
= 3,84 MB/s
```

Damit wird die PSRAM-Bandbreite im Vergleich zu RGB565 sehr stark reduziert.

---

## 7. Niedrige Bildrate ist ausdrücklich erwünscht

Für die Zielanwendungen werden keine 60 Hz benötigt.

Für Bedienoberflächen sind etwa:

- **20 Hz**
- **25 Hz**
- **30 Hz**

vollkommen plausibel.

Eine Maschinensteuerung, ein Messgerät oder ein BHKW-Dashboard benötigt weder Gaming- noch Video-Refreshraten.

Wichtig ist allerdings die Unterscheidung:

### GUI-Update-Rate

GEM muss nur dann neu zeichnen, wenn sich etwas geändert hat.

Beispiel:

- Temperaturwert ändert sich einmal pro Sekunde
- GEM zeichnet den Wert einmal neu
- danach keine weitere CPU-Arbeit

### physische Panel-Refresh-Rate

Das Panel muss seinen Framebuffer trotzdem regelmäßig auslesen.

Der Gewinn einer niedrigen Refresh-Rate entsteht nur dann vollständig, wenn das verwendete LVDS-Panel auch tatsächlich bei z. B. 20–30 Hz betrieben werden kann.

Das Panel muss deshalb gezielt nach seinem zulässigen Pixelclock-/Timingbereich ausgewählt werden.

---

## 8. LVDS-Grundstruktur

Bei klassischem 18-Bit-Single-Link-LVDS werden verwendet:

```text
3 × LVDS-Datenpaare
1 × LVDS-Clockpaar
```

Die drei Datenlanes übertragen pro Pixel jeweils sieben serielle Bits.

Damit gilt grundsätzlich:

```text
LVDS-Bitrate pro Lane = 7 × Pixelclock
```

Die Idee ist, HSTX des RP2350B direkt für diese schnellen Ausgangssignale zu verwenden.

GPIO12–GPIO19 stehen dafür als HSTX-Ausgänge zur Verfügung.

Geplant sind vier differentielle Paare:

```text
DATA0 + / -
DATA1 + / -
DATA2 + / -
CLOCK + / -
```

Die elektrische Anpassung soll nur mit passiven Widerständen erfolgen.

**Kein externer aktiver LVDS-Serializer.**

---

## 9. Warum RGBI interessant für LVDS ist

Bei 18-Bit-LVDS werden pro Pixel RGB666-Daten plus Steuerinformationen auf drei 7-Bit-Lanes verteilt.

Die Kernidee der Evaluierung ist:

Das 4-Bit-RGBI-Format wird so auf RGB666 abgebildet, dass viele Ausgangsbits lediglich Wiederholungen vorhandener Pixelbits sind.

Beispielhafte Expansion:

```text
R5 R4 R3 R2 R1 R0 = R R I I I I
G5 G4 G3 G2 G1 G0 = G G I I I I
B5 B4 B3 B2 B1 B0 = B B I I I I
```

Damit entstehen vier Helligkeitsstufen pro Farbkanal aus nur zwei Informationen:

- Farbbit
- Intensity-Bit

Das reduziert die notwendige Logik beim Scanout erheblich.

Die genaue Bitzuordnung auf die LVDS-Lanes muss für das konkrete Panel und dessen LVDS-Mapping verifiziert werden.

---

## 10. Geplante Hardware-Pipeline

Zielarchitektur:

```text
                 Core 0
             pTOS / GEM
                 |
                 |
                 v
        4-bpp RGBI framebuffer
             in PSRAM
                 |
                 | DMA
                 v
        kleine SRAM-Linebuffer
                 |
                 | DMA
                 v
             PIO-State-
              Machines
                 |
                 v
                HSTX
                 |
          passive Widerstände
                 |
                 v
              LVDS LCD
```

Core 1 kommt im Scanout-Pfad nicht vor.

Ziel:

```text
Core 0 Display-Refresh-Last: ~0 %
Core 1 Display-Refresh-Last: 0 %
```

Core 0 belastet das Display nur dann, wenn GEM tatsächlich neue Pixel in den Framebuffer schreibt.

---

## 11. Linebuffer

Bei 800 Pixel Breite und 4 bpp benötigt eine Zeile:

```text
800 × 0,5 Byte = 400 Byte
```

Damit reichen sehr kleine interne SRAM-Puffer.

Beispiel:

```text
Linebuffer A = 400 Byte
Linebuffer B = 400 Byte
```

Double Buffering für den Scanout benötigt damit nur:

**800 Byte internes SRAM**

Selbst zusätzliche Verwaltungs- und Timingpuffer fallen kaum ins Gewicht.

---

## 12. 800 × 600 bei 20–30 Hz

Als Referenz wurde das klassische 800×600-Timing betrachtet.

Bei ungefähr halbierter Bildrate sinkt auch der benötigte Pixelclock entsprechend.

Grobe Größenordnung:

### ca. 30 Hz

Pixelclock ungefähr:

**~20 MHz**

LVDS-Serialrate:

```text
20 MHz × 7 ≈ 140 MHz
```

Damit liegt man ungefähr im Bereich des nominellen RP2350-Systemtakts.

### ca. 20 Hz

Pixelclock ungefähr:

**~13,3 MHz**

LVDS-Serialrate:

```text
13,3 MHz × 7 ≈ 93 MHz
```

Das gibt deutlich mehr Timingreserve.

Der tatsächliche zulässige Takt hängt aber vom konkreten Panel ab.

---

## 13. Warum 20–30 Hz gut zum Produktziel passen

Die Plattform soll bewusst kein Multimedia-System sein.

Der Trade-off lautet:

**Weniger Farben und niedrigere Bildrate gegen deutlich mehr Rechenleistung für die eigentliche Anwendung.**

Das ist sinnvoll für:

- Steuerungen
- Sensorik
- Regelung
- Robotik
- Maschineninterfaces
- Messgeräte
- batteriebetriebene Geräte

Die eingesparte Display-Rechenleistung steht stattdessen der eigentlichen Anwendung zur Verfügung.

---

## 14. Produktphilosophie

GEMbedded soll nicht versuchen, Raspberry Pi, Linux oder moderne Multimedia-Systeme zu ersetzen.

Die Positionierung ist vielmehr:

> **Ein kleines, sehr schnell startendes und deterministisches Embedded-GUI-System mit echter grafischer Oberfläche, bei dem die Benutzeroberfläche die eigentliche Echtzeit-Anwendung möglichst wenig belastet.**

Oder noch kürzer:

> **GUI statt Multimedia.**

Wer Fotos, Video, hohe Farbtiefe oder aufwendige Animationen braucht, ist mit einer anderen Plattform besser bedient.

Wer dagegen ein robustes, sofort verfügbares grafisches Bedienpanel benötigt, profitiert von der reduzierten Architektur.

---

## 15. Aktueller Favorit

Der derzeit attraktivste Zielpunkt ist:

```text
RP2350B
16 MB PSRAM

800 × 600
4 bpp RGBI
16 Farben

ca. 20–30 Hz

Framebuffer:
240 kB

PSRAM-Scanout-Bandbreite:
4,8–7,2 MB/s

Scanout:
DMA + PIO + HSTX

Core 0:
GEM / pTOS / Anwendung

Core 1:
vollständig für Echtzeitaufgaben frei

externe aktive Display-Hardware:
keine
```

Alternativ ist 800×480 weiterhin interessant, insbesondere wenn hierfür leichter ein geeignetes LVDS-Panel mit niedrigem Pixelclock gefunden wird.

---

## 16. Noch zu verifizieren

Die bisherige Evaluierung zeigt einen plausiblen Architekturpfad, aber vor einem PCB-Design müssen folgende Punkte praktisch bzw. anhand der finalen RP2350- und Panel-Dokumentation verifiziert werden:

1. **Exaktes PIO→HSTX-Coupling**
   - Welche HSTX-Bits können direkt aus welchen PIO-Signalen gespeist werden?
   - Welche DDR-/RAW-Konfiguration ist dafür optimal?

2. **7:1-LVDS-Bitpacking**
   - Exakte PIO-Instruktionsfolge für alle drei Datenlanes
   - Synchronität der drei State Machines

3. **LVDS-Clock**
   - Erzeugung und Phasenlage
   - Verhältnis zur Datenübertragung

4. **Panel-Mapping**
   - JEIDA oder VESA
   - exakte RGB-/DE-/HSYNC-/VSYNC-Zuordnung

5. **Panel-Minimalfrequenz**
   - unterstützt das konkrete Panel tatsächlich 20–30 Hz?
   - minimale Pixelclock laut Datenblatt

6. **Elektrischer Pegel**
   - Widerstandswerte für die pseudo-differentielle HSTX-LVDS-Ausgabe
   - Signalqualität und Common Mode
   - Terminierung des Panels

7. **PSRAM/DMA-Arbitration**
   - Verhalten bei gleichzeitigem GEM-Schreibzugriff
   - ausreichende Reserve für QMI/Flash/PSRAM

8. **Tearing**
   - einfacher Single Buffer
   - Dirty Rectangles
   - optional Double Buffering
   - ggf. Frame-Synchronisation

---

## 17. Nächster sinnvoller Prototyp

Ein erster Hardware-/Firmware-Test sollte möglichst klein sein:

1. geeignetes 800×480- oder 800×600-Single-Link-LVDS-Panel auswählen
2. zunächst feste Testfarben erzeugen
3. HSTX-Differentialausgänge elektrisch testen
4. LVDS-Clock stabilisieren
5. drei Datenlanes mit festem Testmuster senden
6. PIO-basierte RGBI-Erzeugung implementieren
7. SRAM-Framebuffer testen
8. anschließend DMA aus PSRAM zuschalten
9. zuletzt GEMbedded auf den 4-bpp-RGBI-Framebuffer setzen

Damit lässt sich jede Ebene unabhängig validieren.

---

## Fazit

Die reduzierte Farbtiefe ist für dieses Projekt kein Nachteil, sondern ein bewusster Architekturentscheid.

**4-bpp RGBI passt sehr gut zu GEMbedded und zum RP2350B:**

- nur 240 kB bei 800×600
- sehr geringe PSRAM-Bandbreite
- nur 16 Farben, aber für Embedded-UIs ausreichend
- keine Multimedia-Ambitionen
- Core 1 bleibt komplett frei
- Core 0 muss den laufenden Scanout nicht bedienen
- kein externer aktiver Displaycontroller erforderlich
- PIO, DMA und HSTX übernehmen den Displaypfad

Der aktuelle technische Leitgedanke lautet daher:

> **So wenig Display-Overhead wie möglich – so viel Rechenleistung wie möglich für die eigentliche Maschine.**

---

## 18. Referenzen und Herleitung der Machbarkeit

Dieser Abschnitt trennt bewusst zwischen **offiziell dokumentierten RP2350-Funktionen**, einem **existierenden LVDS-Proof-of-Concept** und den Teilen, die für GEMbedded noch experimentell verifiziert werden müssen.

### 18.1 Raspberry Pi RP2350 Datasheet – Primärquelle

**Raspberry Pi Ltd.: RP2350 Datasheet**  
https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf

Für dieses Projekt besonders relevant:

#### HSTX – Kapitel 12.11

HSTX besitzt:

- acht physische HSTX-Ausgänge auf GPIO12–GPIO19,
- DDR-Ausgangsregister,
- eine Bit-Crossbar,
- optionale Invertierung jedes Ausgangs,
- einen Ausgangs-Shifter,
- einen Clock-Generator,
- einen Command Expander,
- sowie einen speziellen **PIO-to-HSTX coupled mode**.

Der für unser Konzept entscheidende Abschnitt ist:

**Section 12.11.6 – PIO-to-HSTX coupled mode**

Dort wird ausdrücklich beschrieben, dass HSTX **bis zu acht PIO-Pinausgänge direkt in die HSTX-Bit-Crossbar übernehmen kann**.

Wichtige Randbedingung:

> `clk_hstx` muss dabei direkt von `clk_sys` gespeist werden.  
> Gleiche Frequenz aus einer anderen Clock-Quelle reicht nicht.

Im Coupled Mode erscheinen die PIO-Ausgänge 12–19 als Crossbar-Quellen 24–31.

Das ist die Grundlage für die geplante Architektur:

```text
PIO
 |
 | vier logische Signale
 | D0 / D1 / D2 / CLK
 v
HSTX Bit-Crossbar
 |
 +--> Ausgang 0 ----+
 |                  +--> LVDS D0 pair
 +--> Ausgang 1 INV-+
 |
 +--> Ausgang 2 ----+
 |                  +--> LVDS D1 pair
 +--> Ausgang 3 INV-+
 |
 +--> Ausgang 4 ----+
 |                  +--> LVDS D2 pair
 +--> Ausgang 5 INV-+
 |
 +--> Ausgang 6 ----+
                    +--> LVDS CLK pair
 +--> Ausgang 7 INV-+
```

Dabei kann jeweils dasselbe PIO-Signal auf zwei HSTX-Ausgänge gelegt und einer davon mit `INV` invertiert werden.

#### HSTX BITx Register

Die Register `BIT0` bis `BIT7` besitzen u. a.:

- `SEL_P`
- `SEL_N`
- `INV`
- `CLK`

Im Coupled Mode wählen `SEL_P` und `SEL_N` mit den Werten 24–31 die acht gekoppelten PIO-Ausgänge.

Damit lässt sich ein logisches Signal auf ein differentielles Ausgangspaar duplizieren und auf einer Seite invertieren.

#### HSTX Clock Generator

Im HSTX-CSR existieren:

- `CLKDIV`
- `CLKPHASE`

Der interne HSTX-Clockgenerator ist prinzipiell interessant.

Für den ersten GEMbedded-Prototyp ist es aber konzeptionell sauberer, den **vierten PIO-State-Machine als LVDS-Clockgenerator** zu verwenden. Damit hängen Daten und Clock an derselben PIO-Zeitbasis und wir müssen nicht davon abhängen, wie der interne HSTX-Shifter den Clockgenerator im reinen Coupled-Mode taktet.

Das reduziert eine mögliche Fehlerquelle beim Bring-up.

---

### 18.2 RP2350 DMA – vollständig autonome Transfers

Im RP2350-Datenblatt:

**Section 12.6 – DMA**

Besonders relevant:

- `CHAIN_TO`
- DREQ-Pacing
- Trigger-Aliase wie `READ_ADDR_TRIG`
- DMA-Control-Blocks
- Ping-Pong- und Ring-Mechanismen
- `TRIGGER_SELF` / `ENDLESS`

Das Datenblatt beschreibt ausdrücklich, dass ein DMA-Kanal nach Abschluss einen anderen DMA-Kanal triggern kann.

Ein Control-DMA-Kanal kann wiederum die Register eines Daten-DMA-Kanals neu laden und ihn erneut starten.

Damit ist folgende Schleife ohne CPU-Interrupt möglich:

```text
DMA DATA
Framebuffer -> PIO FIFO
      |
      | transfer complete
      v
DMA RESTART
schreibt Framebuffer-Startadresse
in READ_ADDR_TRIG von DMA DATA
      |
      +---------------------------> DMA DATA läuft erneut
```

Der RP2350 besitzt außerdem DREQs für alle PIO-TX-FIFOs, zum Beispiel:

```text
DREQ_PIO0_TX0
DREQ_PIO0_TX1
DREQ_PIO0_TX2
DREQ_PIO0_TX3
```

Der DMA kann deshalb vom Füllstand eines PIO-FIFOs gepaced werden.

Das bedeutet:

**Nach der Initialisierung ist kein Interrupt pro Zeile und kein Interrupt pro Frame erforderlich.**

---

### 18.3 RP2350 PIO

Im RP2350-Datenblatt:

**Kapitel 11 – PIO**

PIO unterstützt u. a.:

- vier State Machines pro PIO-Block,
- drei PIO-Blöcke auf RP2350,
- TX/RX-FIFOs,
- Autopull,
- DMA-DREQ,
- deterministische Ausführung mit einem Instruktionszyklus pro PIO-Takt,
- synchrones Starten mehrerer State Machines.

Für unser Design ist besonders interessant, dass **ein einziger PIO-Block vier State Machines besitzt**.

Damit ist folgende Aufteilung möglich:

```text
PIO0 SM0 -> LVDS Data Lane 0
PIO0 SM1 -> LVDS Data Lane 1
PIO0 SM2 -> LVDS Data Lane 2
PIO0 SM3 -> LVDS Clock
```

Alle vier können synchron gestartet werden.

---

### 18.4 QMI / externe PSRAM

Im RP2350-Datenblatt:

**Section 12.14 – QSPI Memory Interface (QMI)**

QMI unterstützt:

- SPI
- Dual-SPI
- Quad-SPI
- zwei Chip-Selects
- memory-mapped Zugriffe
- DMA-Zugriffe
- konfigurierbare Read-/Write-Formate
- hohe sequentielle Transferleistung

QMI ist ausdrücklich als Speicherinterface optimiert und kann externe PSRAM über den XIP-/Memory-Mapping-Pfad einbinden.

---

### 18.5 Verwendete PSRAM

**ISSI IS66WVS16M8FBLL-104NLI**

128 Mbit = 16 MB PSRAM.

Unterstützt:

- SPI
- QPI
- bis 104 MHz
- 2,7–3,6 V bei der BLL-Variante

Referenzen:

ISSI-Produktdaten / Distributor-Daten:  
https://www.mouser.de/de/ProductDetail/ISSI/IS66WVS16M8FBLL-104NLI

SparkFun Produktseite mit Datenblatt-Link:  
https://www.sparkfun.com/16-mb-psram-ic-is66wvs16m8fbll.html

Die theoretische QPI-Rohdatenrate bei 104 MHz beträgt:

```text
104 MHz × 4 Bit / 8
= 52 MB/s
```

Das ist ein theoretischer Maximalwert. Reale Nutzdatenraten sind wegen Command-/Address-Overhead, Busarbitration und Timing geringer.

Für unseren 4-bpp-Framebuffer ist die erforderliche Nutzdatenrate jedoch sehr niedrig:

```text
800 × 600 × 0,5 Byte × 20 Hz = 4,8 MB/s
800 × 600 × 0,5 Byte × 30 Hz = 7,2 MB/s
```

Damit besteht erhebliche Reserve.

---

### 18.6 PicoLVDS – existierender RP2350-LVDS-Proof-of-Concept

**PicoLVDS von ceteras**  
https://github.com/ceteras/PicoLVDS

Das Projekt ist für die Machbarkeit besonders wichtig, weil dort ein RP2350 bereits **direkt ein 1024×600-LVDS-Panel über HSTX** ansteuert.

Getestete Displays laut Projekt:

- AUO B089AW01-V.0
- HSD089IFW1-A00-V1.0

Der Autor verwendet:

- RP2350 / Pico 2
- HSTX
- keine aktive LVDS-Bridge
- 270-Ohm-Widerstände ähnlich der Pico-DVI-Schaltung
- 18-Bit-LVDS
- 3 Datenlanes + Clock
- 4-bpp-Farbdarstellung

Forum-Thread:

https://forums.raspberrypi.com/viewtopic.php?t=398182

Dort beschreibt der Autor, dass die mit 270-Ohm-Widerständen erzeugten Signale von den getesteten LVDS-Panels akzeptiert werden.

Er weist gleichzeitig darauf hin, dass seine konkrete Implementierung für die Zeilenerzeugung CPU-intensiv ist und bei 300 MHz ungefähr 50 fps erreicht.

**Genau diesen CPU-intensiven Teil wollen wir bei GEMbedded nicht übernehmen.**

---

### 18.7 PicoLVDS – relevantes Sourcefile

Direkter Quelltext:

https://raw.githubusercontent.com/ceteras/PicoLVDS/main/firmware/hstx_lvds.c

Besonders relevant sind die Kommentare am Anfang der Datei.

Dort ist die klassische 18-Bit-LVDS-Aufteilung dokumentiert:

```text
D0: G0 R5 R4 R3 R2 R1 R0
D1: B1 B0 G5 G4 G3 G2 G1
D2: EN x1 x1 B5 B4 B3 B2
CK: 1  1  0  0  0  1  1
```

Außerdem beschreibt der Code das zentrale 7:1-Problem:

LVDS benötigt **7 serielle Bits pro Pixel und Lane**, während die dort verwendete HSTX-DDR-Packung mit geraden Bitgruppen arbeitet.

PicoLVDS löst das durch das Packen von acht aufeinanderfolgenden Pixeln in sieben HSTX-Transfers.

Interessant ist außerdem, dass PicoLVDS ebenfalls einen **4-Bit-RGBI-Framebuffer** verwendet und per LUT jeweils zwei 4-Bit-Pixel in LVDS-Wörter umsetzt.

Das ist ein starker Hinweis darauf, dass **4 bpp eine sinnvolle Größenordnung für einen RP2350-LVDS-Framebuffer ist**.

---

## 19. Was davon bereits bewiesen ist – und was nicht

### Durch Herstellerdokumentation eindeutig belegt

- HSTX existiert auf GPIO12–GPIO19.
- HSTX besitzt eine Bit-Crossbar und Ausgangsinvertierung.
- PIO-Ausgänge können im Coupled Mode direkt auf die HSTX-Crossbar geführt werden.
- Coupled Mode kann acht PIO-Signale übernehmen.
- PIO und HSTX müssen dabei direkt dieselbe `clk_sys`-Quelle verwenden.
- DMA kann PIO-FIFOs per DREQ beliefern.
- DMA-Kanäle können sich gegenseitig chainen und neu triggern.
- QMI kann externe Quad-SPI-PSRAM memory-mapped anbinden.

### Durch existierenden Hardware-Prototyp belegt

- Ein RP2350 kann ein klassisches 18-Bit-Single-Link-LVDS-Panel direkt ansteuern.
- Drei LVDS-Datenpaare plus Clock sind mit den acht HSTX-Pins darstellbar.
- Ein einfacher passiver Widerstandsansatz kann bei realen Panels funktionieren.
- 1024×600 wurde praktisch demonstriert.
- 4-bpp-Farbframebuffer wurde praktisch demonstriert.

### Unser eigener, noch zu beweisender Designschritt

Noch **nicht** als fertige Implementierung bewiesen ist:

> Ein PIO-Programm, das unseren 4-bpp-RGBI-Framebuffer direkt und vollständig autonom in die drei 7-Bit-LVDS-Datenströme umsetzt, ohne laufende CPU-Unterstützung.

Das ist der wichtigste Punkt für den Prototyp.

Die Hardware besitzt alle benötigten Bausteine. Ob die gewählte RGBI-Bitorganisation in ein ausreichend kleines und zeitlich sauberes PIO-Programm passt, muss mit einem realen PIO-Programm und Logic-Analyzer/Oszilloskop verifiziert werden.

Die Design-Notiz versteht die Architektur daher als:

**sehr plausibel, aber noch nicht als fertig bewiesenen GEMbedded-Treiber.**

---

# 20. Initialisierungs-Pseudocode

Die folgenden Snippets sind bewusst **Pseudocode bzw. SDK-nahe Skizzen**.

Sie zeigen die geplante Hardwareinitialisierung und die autonome Betriebsweise. Register-/Makronamen müssen beim Implementieren gegen die verwendete Pico-SDK-Version geprüft werden.

---

## 20.1 Gesamtinitialisierung

```c
void lvds_display_init(void)
{
    // 1. Zielclock festlegen.
    //
    // Beispiel:
    // 800x600 @ ~30 Hz -> Pixelclock grob ~20 MHz
    // 7 LVDS-Bits / Pixel -> Bitclock grob ~140 MHz
    //
    // Coupled Mode verlangt:
    // clk_hstx muss DIREKT clk_sys sein.

    clock_init_for_lvds(LVDS_BIT_CLOCK_HZ);

    // 2. PSRAM/QMI initialisieren.
    psram_init_qpi_104mhz();

    // 3. HSTX-fähige GPIOs vorbereiten.
    hstx_gpio_init();

    // 4. PIO-Programme laden, aber State Machines noch nicht starten.
    lvds_pio_init();

    // 5. DMA-Kanäle vorbereiten.
    lvds_dma_init();

    // 6. PIO -> HSTX Coupled Mode konfigurieren.
    hstx_coupled_init();

    // 7. DMA an den Framebuffer hängen.
    lvds_dma_set_framebuffer(framebuffer);

    // 8. Alle vier PIO-State-Machines taktgleich starten.
    lvds_pio_start_synchronised();

    // 9. DMA starten.
    //
    // Ab hier soll der Scanout autonom laufen.
    lvds_dma_start();
}
```

Danach soll der normale Code nur noch in den Framebuffer zeichnen:

```c
for (;;) {
    gem_event_loop();

    // Core 0:
    // GEM zeichnet nur dann, wenn sich UI-Inhalte ändern.
    //
    // Core 1:
    // unabhängig für Echtzeitaufgaben verfügbar.
}
```

---

## 20.2 Clock-Konfiguration

Pseudocode:

```c
static void clock_init_for_lvds(uint32_t bit_clock_hz)
{
    // clk_sys = gewünschte LVDS-Bitrate
    set_sys_clock_hz(bit_clock_hz, true);

    // WICHTIG:
    // clk_hstx darf nicht nur zufällig dieselbe Frequenz haben.
    // Laut Datasheet muss HSTX im Coupled Mode DIREKT clk_sys wählen.

    clock_configure(
        clk_hstx,
        CLK_HSTX_AUXSRC_CLK_SYS,
        bit_clock_hz,
        bit_clock_hz
    );
}
```

Für ungefähr 800×600 bei 30 Hz:

```text
Pixelclock ≈ 20 MHz
LVDS serial ≈ 7 × 20 MHz
            ≈ 140 MHz
```

Damit kann das Design prinzipiell im Bereich des nominellen RP2350-Systemtakts bleiben.

---

## 20.3 HSTX-GPIOs

```c
static void hstx_gpio_init(void)
{
    for (int gpio = 12; gpio <= 19; ++gpio) {
        gpio_set_function(gpio, GPIO_FUNC_HSTX);

        // Startwert konservativ wählen.
        // Drive Strength / Slew Rate später am Oszilloskop optimieren.
        pads_set_drive_strength(gpio, DRIVE_4MA_OR_8MA);
        pads_set_slew_fast(gpio, true);
    }
}
```

Die geplante externe Beschaltung bleibt passiv:

```text
RP2350B                         Panel

GPIO12 ---- R ---- D0+
GPIO13 ---- R ---- D0-

GPIO14 ---- R ---- D1+
GPIO15 ---- R ---- D1-

GPIO16 ---- R ---- D2+
GPIO17 ---- R ---- D2-

GPIO18 ---- R ---- CK+
GPIO19 ---- R ---- CK-
```

Startpunkt für die Versuche:

```text
R ~= 270 Ohm
```

Dieser Wert stammt aus dem PicoLVDS-Prototyp bzw. aus der Pico-DVI-Tradition.

**Er ist für unser endgültiges PCB nicht automatisch als optimal anzusehen.**

Empfohlen wird ein Footprint, der unterschiedliche Werte erlaubt, z. B.:

```text
270 / 330 / 390 Ohm
```

und eine Messung der differentiellen Amplitude / Common-Mode-Spannung am realen Panel.

---

## 20.4 PIO-Aufteilung

Erster konzeptioneller Ansatz:

```c
PIO pio = pio0;

enum {
    SM_D0  = 0,
    SM_D1  = 1,
    SM_D2  = 2,
    SM_CLK = 3
};
```

Die logischen PIO-Ausgänge werden auf vier der PIO-Pinbits 12–19 gelegt.

Beispiel:

```text
PIO output 12 -> logical D0
PIO output 13 -> logical D1
PIO output 14 -> logical D2
PIO output 15 -> logical CLK
```

Diese erscheinen im HSTX Coupled Mode als Crossbar-Quellen:

```text
PIO12 -> HSTX source 24
PIO13 -> HSTX source 25
PIO14 -> HSTX source 26
PIO15 -> HSTX source 27
```

---

## 20.5 Synchronisierte PIO-Initialisierung

```c
static void lvds_pio_init(void)
{
    uint off_d0  = pio_add_program(pio0, &lvds_d0_program);
    uint off_d1  = pio_add_program(pio0, &lvds_d1_program);
    uint off_d2  = pio_add_program(pio0, &lvds_d2_program);
    uint off_clk = pio_add_program(pio0, &lvds_clk_program);

    pio_sm_config c0 = lvds_d0_program_get_default_config(off_d0);
    pio_sm_config c1 = lvds_d1_program_get_default_config(off_d1);
    pio_sm_config c2 = lvds_d2_program_get_default_config(off_d2);
    pio_sm_config cc = lvds_clk_program_get_default_config(off_clk);

    // Coupled Mode läuft synchron zu clk_sys.
    sm_config_set_clkdiv(&c0, 1.0f);
    sm_config_set_clkdiv(&c1, 1.0f);
    sm_config_set_clkdiv(&c2, 1.0f);
    sm_config_set_clkdiv(&cc, 1.0f);

    // Autopull für Framebufferdaten.
    sm_config_set_out_shift(&c0, SHIFT_DIRECTION, true, PULL_THRESHOLD);
    sm_config_set_out_shift(&c1, SHIFT_DIRECTION, true, PULL_THRESHOLD);
    sm_config_set_out_shift(&c2, SHIFT_DIRECTION, true, PULL_THRESHOLD);

    // Mehr TX-FIFO-Tiefe.
    sm_config_set_fifo_join(&c0, PIO_FIFO_JOIN_TX);
    sm_config_set_fifo_join(&c1, PIO_FIFO_JOIN_TX);
    sm_config_set_fifo_join(&c2, PIO_FIFO_JOIN_TX);

    // Je eine logische Leitung.
    sm_config_set_out_pins(&c0, 12, 1);
    sm_config_set_out_pins(&c1, 13, 1);
    sm_config_set_out_pins(&c2, 14, 1);
    sm_config_set_out_pins(&cc, 15, 1);

    pio_sm_init(pio0, SM_D0,  off_d0,  &c0);
    pio_sm_init(pio0, SM_D1,  off_d1,  &c1);
    pio_sm_init(pio0, SM_D2,  off_d2,  &c2);
    pio_sm_init(pio0, SM_CLK, off_clk, &cc);

    // Noch nicht starten.
}
```

Alle State Machines werden später in einem Schritt synchron gestartet:

```c
static void lvds_pio_start_synchronised(void)
{
    uint32_t mask =
        (1u << SM_D0) |
        (1u << SM_D1) |
        (1u << SM_D2) |
        (1u << SM_CLK);

    pio_enable_sm_mask_in_sync(pio0, mask);
}
```

---

## 20.6 HSTX Coupled Mode – Crossbar

Konzeptioneller Pseudocode:

```c
#define PIO12_SOURCE 24
#define PIO13_SOURCE 25
#define PIO14_SOURCE 26
#define PIO15_SOURCE 27

static uint32_t hstx_data_source(int source, bool invert)
{
    return
        HSTX_SEL_P(source) |
        HSTX_SEL_N(source) |
        (invert ? HSTX_INV : 0);
}
```

Dann:

```c
static void hstx_coupled_init(void)
{
    // HSTX zunächst stoppen.
    hstx_ctrl_hw->csr = 0;

    // D0 differentiell
    hstx_ctrl_hw->bit[0] = hstx_data_source(PIO12_SOURCE, false);
    hstx_ctrl_hw->bit[1] = hstx_data_source(PIO12_SOURCE, true);

    // D1 differentiell
    hstx_ctrl_hw->bit[2] = hstx_data_source(PIO13_SOURCE, false);
    hstx_ctrl_hw->bit[3] = hstx_data_source(PIO13_SOURCE, true);

    // D2 differentiell
    hstx_ctrl_hw->bit[4] = hstx_data_source(PIO14_SOURCE, false);
    hstx_ctrl_hw->bit[5] = hstx_data_source(PIO14_SOURCE, true);

    // Clock differentiell
    hstx_ctrl_hw->bit[6] = hstx_data_source(PIO15_SOURCE, false);
    hstx_ctrl_hw->bit[7] = hstx_data_source(PIO15_SOURCE, true);

    hstx_ctrl_hw->csr =
          HSTX_CSR_COUPLED_MODE
        | HSTX_CSR_COUPLED_SEL(0)    // PIO0
        | HSTX_CSR_ENABLE;
}
```

Wichtig:

Die echten Pico-SDK-Makronamen können anders heißen. Das Prinzip entspricht dem Datenblatt:

```text
SEL_P = coupled PIO source
SEL_N = coupled PIO source
INV   = 1 auf einer Seite des Paares
```

Da `SEL_P == SEL_N` ist, bleibt das Signal über beide DDR-Halbzyklen gleich.

Wir verwenden HSTX hier also zunächst effektiv als:

**schnelle, skew-arme, invertierbare Ausgangs-Crossbar für PIO.**

---

## 20.7 LVDS-Clock als vierte PIO-State-Machine

Für 7 serielle Bitzeiten pro Pixel muss die LVDS-Clock ein 7-Bit-Pattern erzeugen.

PicoLVDS verwendet beispielsweise sinngemäß:

```text
1 1 0 0 0 1 1
```

Je nach gewählter Phasenlage kann das Pattern rotiert werden.

Ein konzeptionelles PIO-Programm:

```asm
.program lvds_clk

.wrap_target

    set pins, 1
    nop
    set pins, 0
    nop
    nop
    set pins, 1
    nop

.wrap
```

Das sind exakt sieben PIO-Zyklen pro Pixel.

Bei:

```text
clk_sys = 140 MHz
```

ergibt das:

```text
Pixelclock = 140 MHz / 7
           = 20 MHz
```

Die exakte Clock-Phase gegenüber D0/D1/D2 muss am realen Panel verifiziert und ggf. durch Rotation bzw. PIO-Programmanpassung verschoben werden.

---

## 20.8 Beispiel für die Idee des RGBI-Lane-Generators

**Achtung: nur algorithmische Skizze, noch kein fertiges PIO-Programm.**

Nach geeigneter Rotation einer LVDS-Lane kann beispielsweise eine Sequenz entstehen wie:

```text
R R I I I I I
```

Wenn das RGBI-Nibble so im OSR liegt, dass zunächst `R`, danach zwei nicht benötigte Bits und anschließend `I` kommen, kann PIO Wiederholungen kostenlos durch das Halten des letzten Pinzustands erzeugen.

Konzeptionell:

```asm
; 7 Zyklen = 1 Pixel

out pins, 1       ; R
out null, 2       ; Pin bleibt R, zwei andere RGBI-Bits überspringen
out pins, 1       ; I
nop               ; I halten
nop               ; I halten
nop               ; I halten
nop               ; I halten
```

Ergebnis:

```text
R R I I I I I
```

Das illustriert, warum das 4-bpp-RGBI-Format für PIO interessant ist:

**Wiederholte Ausgangsbits kosten keine zusätzlichen Framebufferbits.**

Die tatsächlichen Programme für D0, D1 und D2 müssen noch so entworfen werden, dass:

- alle drei Lanes dieselbe 7-Bit-Phasenlage besitzen,
- dieselbe RGBI-Speicherordnung verwendet werden kann,
- DE/Blanking korrekt erzeugt wird,
- kein Pixelbit mehrfach aus dem OSR benötigt wird, ohne vorher sinnvoll gepuffert zu werden,
- das Programm in das PIO-Instruktionsbudget passt.

Das ist die zentrale Firmware-Evaluierungsaufgabe.

---

## 20.9 DMA – autonomer Frame-Loop

Das folgende Prinzip ist durch die DMA-Trigger-/Chaining-Funktionen des RP2350 abgedeckt.

Wir verwenden:

```text
DMA A = Datenkanal
DMA B = Restart-/Control-Kanal
```

Pseudocode:

```c
static uint32_t framebuffer_start;

static void dma_stream_init(
    int dma_data,
    int dma_restart,
    volatile void *pio_fifo,
    int pio_dreq,
    void *framebuffer,
    uint32_t transfer_count)
{
    framebuffer_start = (uint32_t)framebuffer;

    dma_channel_config data_cfg =
        dma_channel_get_default_config(dma_data);

    channel_config_set_transfer_data_size(
        &data_cfg,
        DMA_SIZE_8
    );

    channel_config_set_read_increment(&data_cfg, true);
    channel_config_set_write_increment(&data_cfg, false);

    // DMA wartet auf Platz im PIO-TX-FIFO.
    channel_config_set_dreq(&data_cfg, pio_dreq);

    // Nach einem Frame Restart-Kanal auslösen.
    channel_config_set_chain_to(&data_cfg, dma_restart);

    dma_channel_configure(
        dma_data,
        &data_cfg,
        pio_fifo,
        framebuffer,
        transfer_count,
        false
    );


    dma_channel_config restart_cfg =
        dma_channel_get_default_config(dma_restart);

    channel_config_set_transfer_data_size(
        &restart_cfg,
        DMA_SIZE_32
    );

    channel_config_set_read_increment(&restart_cfg, false);
    channel_config_set_write_increment(&restart_cfg, false);

    dma_channel_configure(
        dma_restart,
        &restart_cfg,

        // Schreiben in Trigger-Alias des Datenkanals:
        &dma_hw->ch[dma_data].al3_read_addr_trig,

        &framebuffer_start,

        1,
        false
    );
}
```

Ablauf:

```text
1. DATA liest Framebuffer.
2. DATA wird vom PIO-DREQ gepaced.
3. DATA erreicht transfer_count == 0.
4. DATA chained auf RESTART.
5. RESTART schreibt framebuffer_start nach READ_ADDR_TRIG.
6. Dieser Write startet DATA automatisch neu.
7. Zurück zu Schritt 1.
```

Damit entsteht:

```text
         +------------------------------+
         |                              |
         v                              |
Framebuffer -> DMA DATA -> PIO -> HSTX |
                   |                    |
                   v                    |
               DMA RESTART -------------+
```

Kein Frame-Interrupt ist erforderlich.

---

## 20.10 Drei Daten-SMs – erster einfacher Prototyp

Der einfachste autonome Versuchsaufbau kann zunächst **drei getrennte DMA-Datenkanäle** verwenden:

```text
Framebuffer -> DMA0 -> SM0 -> D0
Framebuffer -> DMA1 -> SM1 -> D1
Framebuffer -> DMA2 -> SM2 -> D2

                         SM3 -> CLK
```

Damit wird derselbe 4-bpp-Framebuffer dreimal gelesen.

Die PSRAM-Last wäre dann bei 800×600:

### 20 Hz

```text
4,8 MB/s × 3
= 14,4 MB/s
```

### 30 Hz

```text
7,2 MB/s × 3
= 21,6 MB/s
```

Auch das liegt noch deutlich unter der theoretischen QPI-Rohdatenrate der 104-MHz-PSRAM.

Das ist für einen **ersten Proof-of-Concept möglicherweise einfacher** als eine komplexe DMA-/Linebuffer-Verteilung.

Später kann optimiert werden:

```text
PSRAM
 |
 | nur 1× lesen
 v
SRAM-Linebuffer
 |
 +--> DMA -> SM0
 +--> DMA -> SM1
 +--> DMA -> SM2
```

Damit sinkt die PSRAM-Last wieder auf 4,8–7,2 MB/s.

---

## 20.11 Testmodus vor dem Framebuffer

Vor der vollständigen RGBI-Implementierung sollte zuerst ein vollständig synthetischer Testmodus implementiert werden:

```text
PIO SM0 -> festes D0-Muster
PIO SM1 -> festes D1-Muster
PIO SM2 -> DE + festes D2-Muster
PIO SM3 -> LVDS-Clock
```

Testreihenfolge:

```text
1. LVDS-Clock prüfen
2. differentielle Pegel prüfen
3. Lane-Synchronität prüfen
4. DE/Blanking prüfen
5. feste Farbe darstellen
6. Color Bars
7. SRAM-Framebuffer
8. PSRAM + DMA
9. GEMbedded
```

Damit lassen sich elektrische Probleme sauber von PIO-/Framebuffer-Problemen trennen.

---

# 21. Erwarteter Betrieb nach der Initialisierung

Wenn der autonome Pfad funktioniert, sieht der normale Laufzeitbetrieb so aus:

```text
BOOT
 |
 +--> clocks init
 +--> PSRAM init
 +--> PIO init
 +--> HSTX coupled init
 +--> DMA init
 +--> DMA start
 |
 +==============================+
 | Display läuft ab hier autonom |
 +==============================+
 |
 +--> GEM / pTOS starten
 |
 +--> Core 0: UI / Anwendungen
 |
 +--> Core 1: Echtzeitaufgaben
```

Der Displaypfad läuft unabhängig:

```text
PSRAM -> DMA -> PIO -> HSTX -> LVDS
```

Die CPU wird nur aktiv, wenn GEM tatsächlich den Inhalt des Framebuffers verändert.

Genau dieses Verhalten ist das Architekturziel:

> **Initialisieren, starten, vergessen.**

---

## 22. Praktischer Status der Idee

### Hohe Sicherheit

- direkte RP2350-HSTX-LVDS-Ausgabe ist grundsätzlich möglich;
- HSTX-Coupled-Mode ist ein offizielles RP2350-Feature;
- DMA kann selbständig in PIO streamen und über Control-Channels neu gestartet werden;
- 4 bpp reduziert Speicher- und Buslast massiv;
- 20–30 Hz passen sehr gut zu einer Maschinen-GUI.

### Noch experimentell

- endgültiges RGBI-Bitmapping;
- konkretes PIO-Programm für alle drei LVDS-Lanes;
- minimale Panel-Refresh-/Pixelclock;
- elektrische Widerstandswerte;
- Timing- und Clockphase;
- Verhalten bei gleichzeitigen PSRAM-Zugriffen durch GEM und DMA.

Diese Punkte sind für einen Hardwareprototyp überschaubar und einzeln testbar.
