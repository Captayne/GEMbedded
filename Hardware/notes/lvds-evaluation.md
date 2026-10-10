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
