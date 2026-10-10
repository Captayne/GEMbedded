# GEMbedded – EP5008S / ST7701S RGB565 Bring-up Notes

**Projekt:** GEMbedded auf RP2350B  
**Display:** EP5008S, 5", 480×854, ST7701S, paralleles RGB-Interface  
**Datum der Evaluierung:** 25.09.2026  
**Zweck dieses Dokuments:** Wiedereinstieg in das Thema, sobald die zwei bestellten Display-Samples eingetroffen sind.

---

# 1. Zielbild

Das Display soll als grafisches Embedded-Bedienpanel für GEMbedded dienen.

Zielanwendungen sind insbesondere:

- Maschinenbedienpanel
- Home-Automation-Dashboard
- Akku-Gerätepanel
- Waschmaschine / Haushaltsgerät
- BHKW-/Heizungssteuerung
- Mini-Robotersteuerung
- 3D-Drucker
- Staubsauger
- mobiles Messgerät
- Drucker
- Klimagerät
- CNC-/Industrie-Bedienung

Das System ist **keine Multimedia-Plattform**.

Die zentrale Architekturvorgabe lautet:

```text
Core 0:
pTOS + GEM + GEMbedded + Anwendungen

Core 1:
für Echtzeitaufgaben vollständig frei

Display-Scanout:
PSRAM -> DMA -> PIO -> paralleles RGB -> ST7701S

laufende CPU-Last für Scanout:
nahe 0 %
```

Es soll **kein externer LVDS-Serializer, kein FPGA und kein zusätzlicher Displaycontroller** verwendet werden.

---

# 2. Identifikation des Displays

Aus den Produktbildern und dem FPC-Aufdruck ergibt sich sehr wahrscheinlich:

```text
Serie:       EP5008S
Auflösung:   480 × 854
Größe:       5 Zoll
Controller:  ST7701S
Interface:   paralleles RGB
RGB-Modi:    RGB888 / RGB666 / RGB565
Touch:       optional GT911
FPC:         40 Pin
```

Der relevante Display-Stecker stellt alle RGB-Datenleitungen sowie DCLK, HSYNC, VSYNC und DE heraus.

Pinbelegung laut Händler-/Herstellerbild:

```text
Pin  1      LED-
Pin  2      LED+
Pin  3      GND
Pin  4      IOVCC

Pin  5-12   R0-R7
Pin 13-20   G0-G7
Pin 21-28   B0-B7

Pin 29      GND
Pin 30      DCLK
Pin 31      IOVCC
Pin 32      HSYNC
Pin 33      VSYNC
Pin 34      DE
Pin 35      VCC
Pin 36      GND
Pin 37      /RESET
Pin 38      /CS
Pin 39      SCL
Pin 40      SDA
```

Der Touchscreen besitzt einen separaten 6-Pin-Anschluss:

```text
1 GND
2 RESET
3 INT
4 SDA
5 SCL
6 VCC
```

---

# 3. Originale Testparameter aus dem EP5008S-Hersteller-Screenshot

Der mitgelieferte Screenshot enthält einen Testsatz für das Panel:

```text
Name:       EP5008S

X:          480
Y:          854
Interface:  24 Bit

Vertical:
VBP  = 30
VFP  = 12
VSPW = 1

Horizontal:
HBP  = 30
HFP  = 12
HSPW = 6

DCLK polarity:   Rising
VSYNC polarity:  Falling
HSYNC polarity:  Falling
DEN polarity:    Falling

DCLK frequency:  26.3 MHz

SPI init:        S3W
reserved:        0
init routine:    JLT5008A_init
```

Damit ergibt sich das externe RGB-Timing:

```text
Horizontal total:
480 + 30 + 12 + 6 = 528 DCLK

Vertical total:
854 + 30 + 12 + 1 = 897 lines
```

Pixelclocks pro Frame:

```text
528 × 897 = 473,616
```

Mit dem Hersteller-DCLK von 26,3 MHz:

```text
26,300,000 / 473,616
≈ 55.53 Hz
```

Der Hersteller-Testbetrieb liegt damit bei ungefähr **55,5 Hz**, nicht exakt bei 60 Hz.

---

# 4. Originaler ST7701S-Initialisierungscode aus dem Baidu-File

Die folgende Initialisierung stammt aus der bereitgestellten Datei `EP5008S初始化.c`.

Wichtig:

**Diese Sequenz ist der Hersteller-Referenzcode für das EP5008S und sollte für den ersten Bring-up zunächst weitgehend unverändert übernommen werden.**

Nur die Pixelformat-Umschaltung auf RGB565 wird separat in Abschnitt 6 beschrieben.

---

## 4.1 3-Wire-SPI-Prinzip

Der Referenzcode verwendet eine serielle 3-Wire-artige Initialisierung.

Registerzugriff:

```c
void SPI_WR_REG(uint8_t reg)
{
    CS = 0;

    SDA = 0;       // Command marker
    SCL = 0;
    SCL = 1;

    SPI_SendData(reg);

    CS = 1;
}
```

Datenzugriff:

```c
void SPI_WR_DATA(uint8_t data)
{
    CS = 0;

    SDA = 1;       // Data marker
    SCL = 0;
    SCL = 1;

    SPI_SendData(data);

    CS = 1;
}
```

Die Initialisierung geschieht nur beim Start. Danach läuft das Bild über das parallele RGB-Interface.

---

# 5. Wichtige Register der EP5008S-Initialisierung

## 5.1 Bank 0 / Display Control

Bank auswählen:

```c
FF: 77 01 00 00 10
```

Danach:

```text
C0 = E9 03
C1 = 11 02
C2 = 01 08
CC = 18
```

Originalcode:

```c
SPI_WR_REG(0xFF);
SPI_WR_DATA(0x77);
SPI_WR_DATA(0x01);
SPI_WR_DATA(0x00);
SPI_WR_DATA(0x00);
SPI_WR_DATA(0x10);

SPI_WR_REG(0xC0);
SPI_WR_DATA(0xE9);
SPI_WR_DATA(0x03);

SPI_WR_REG(0xC1);
SPI_WR_DATA(0x11);
SPI_WR_DATA(0x02);

SPI_WR_REG(0xC2);
SPI_WR_DATA(0x01);
SPI_WR_DATA(0x08);

SPI_WR_REG(0xCC);
SPI_WR_DATA(0x18);
```

### Interpretation

`C0 = E9 03` passt exakt zur vertikalen Auflösung 854.

Die ST7701S-Formel ergibt:

```text
((0x69 + 1) × 8) + (3 × 2)
= 854
```

`C1 = 11 02` setzt interne vertikale Porch-/Timingparameter.

`C2 = 01 08` enthält u. a. den RTNI-Wert `8`.

Wichtig:

Die internen ST7701S-Timingregister dürfen **nicht einfach mit den externen RGB-Bus-Porches gleichgesetzt werden**.

Der Hersteller verwendet extern:

```text
VBP 30
VFP 12
VSYNC 1
```

während intern `C1 = 11 02` gesetzt wird.

Für den ersten Test sollen daher:

- interne ST7701S-Werte unverändert bleiben
- externe PIO-Timings gemäß Hersteller-Testdatensatz verwendet werden

---

# 5.2 Gamma Register

Positive Gamma:

```text
B0:
00 0D 14 0D 10 05 02 08
08 1E 05 13 11 A3 29 18
```

Negative Gamma:

```text
B1:
00 0C 14 0C 10 05 03 08
07 20 05 13 11 A4 29 18
```

Originalcode:

```c
SPI_WR_REG(0xB0);
SPI_WR_DATA(0x00);
SPI_WR_DATA(0x0D);
SPI_WR_DATA(0x14);
SPI_WR_DATA(0x0D);
SPI_WR_DATA(0x10);
SPI_WR_DATA(0x05);
SPI_WR_DATA(0x02);
SPI_WR_DATA(0x08);
SPI_WR_DATA(0x08);
SPI_WR_DATA(0x1E);
SPI_WR_DATA(0x05);
SPI_WR_DATA(0x13);
SPI_WR_DATA(0x11);
SPI_WR_DATA(0xA3);
SPI_WR_DATA(0x29);
SPI_WR_DATA(0x18);

SPI_WR_REG(0xB1);
SPI_WR_DATA(0x00);
SPI_WR_DATA(0x0C);
SPI_WR_DATA(0x14);
SPI_WR_DATA(0x0C);
SPI_WR_DATA(0x10);
SPI_WR_DATA(0x05);
SPI_WR_DATA(0x03);
SPI_WR_DATA(0x08);
SPI_WR_DATA(0x07);
SPI_WR_DATA(0x20);
SPI_WR_DATA(0x05);
SPI_WR_DATA(0x13);
SPI_WR_DATA(0x11);
SPI_WR_DATA(0xA4);
SPI_WR_DATA(0x29);
SPI_WR_DATA(0x18);
```

Diese Werte sollten für den ersten Test **unverändert übernommen werden**.

---

# 5.3 Bank 1 / Power Control

Bank auswählen:

```text
FF = 77 01 00 00 11
```

Power-/VCOM-Werte:

```text
B0 = 6C
B1 = 43
B2 = 87
B3 = 80
B5 = 47
B7 = 85
B8 = 20
B9 = 10
C1 = 78
C2 = 78
D0 = 88
```

Original:

```c
SPI_WR_REG(0xFF);
SPI_WR_DATA(0x77);
SPI_WR_DATA(0x01);
SPI_WR_DATA(0x00);
SPI_WR_DATA(0x00);
SPI_WR_DATA(0x11);

SPI_WR_REG(0xB0);
SPI_WR_DATA(0x6C);

SPI_WR_REG(0xB1);
SPI_WR_DATA(0x43);

SPI_WR_REG(0xB2);
SPI_WR_DATA(0x87);

SPI_WR_REG(0xB3);
SPI_WR_DATA(0x80);

SPI_WR_REG(0xB5);
SPI_WR_DATA(0x47);

SPI_WR_REG(0xB7);
SPI_WR_DATA(0x85);

SPI_WR_REG(0xB8);
SPI_WR_DATA(0x20);

SPI_WR_REG(0xB9);
SPI_WR_DATA(0x10);

SPI_WR_REG(0xC1);
SPI_WR_DATA(0x78);

SPI_WR_REG(0xC2);
SPI_WR_DATA(0x78);

SPI_WR_REG(0xD0);
SPI_WR_DATA(0x88);

delay_ms(100);
```

Auch diese Werte zunächst **nicht verändern**.

---

# 5.4 GIP Settings

Der Originalcode programmiert danach die Gate-In-Panel-Sequenz.

Diese Werte sind stark panelspezifisch und sollten unverändert übernommen werden.

```text
E0 = 00 00 02

E1 =
08 00 0A 00 07 00 09 00 00 33 33

E2 =
00 00 00 00 00 00 00 00 00 00 00 00 00

E3 =
00 00 33 33

E4 =
44 44

E5 =
0E 60 A0 A0
10 60 A0 A0
0A 60 A0 A0
0C 60 A0 A0

E6 =
00 00 33 33

E7 =
44 44

E8 =
0D 60 A0 A0
0F 60 A0 A0
09 60 A0 A0
0B 60 A0 A0

EB =
02 01 E4 E4 44 00 40

EC =
02 01

ED =
AB 89 76 54
01 FF FF FF
FF FF FF 10
45 67 98 BA

EF =
08 08 08 45 3F 54
```

Diese Register betreffen das interne Panel-/Gate-Timing.

**Nicht für die 20-/25-/30-Hz-Experimente verändern.**

Die Experimente sollen zunächst ausschließlich über den externen RGB-DCLK erfolgen.

---

# 5.5 Sleep-Out / Display-On

Das Ende des Hersteller-Inits lautet:

```c
// Bank 0
SPI_WR_REG(0xFF);
SPI_WR_DATA(0x77);
SPI_WR_DATA(0x01);
SPI_WR_DATA(0x00);
SPI_WR_DATA(0x00);
SPI_WR_DATA(0x00);

SPI_WR_REG(0x11);      // Sleep Out
delay_ms(120);

// temporäre interne Sequenz
SPI_WR_REG(0xFF);
SPI_WR_DATA(0x77);
SPI_WR_DATA(0x01);
SPI_WR_DATA(0x00);
SPI_WR_DATA(0x00);
SPI_WR_DATA(0x13);

SPI_WR_REG(0xE8);
SPI_WR_DATA(0x00);
SPI_WR_DATA(0x0C);

delay_ms(10);

SPI_WR_REG(0xE8);
SPI_WR_DATA(0x00);
SPI_WR_DATA(0x00);

// zurück zu Bank 0
SPI_WR_REG(0xFF);
SPI_WR_DATA(0x77);
SPI_WR_DATA(0x01);
SPI_WR_DATA(0x00);
SPI_WR_DATA(0x00);
SPI_WR_DATA(0x00);

SPI_WR_REG(0x29);      // Display ON

SPI_WR_REG(0x3A);
SPI_WR_DATA(0x77);     // Original: RGB888 / 24 Bit

SPI_WR_REG(0x36);
SPI_WR_DATA(0x08);     // MADCTL/BGR laut Hersteller

delay_ms(20);
```

---

# 6. Unsere geplante Änderung: RGB565

Der Hersteller verwendet:

```c
SPI_WR_REG(0x3A);
SPI_WR_DATA(0x77);
```

Das ist die mitgelieferte 24-Bit-Konfiguration.

Für GEMbedded soll stattdessen zunächst getestet werden:

```c
SPI_WR_REG(0x3A);
SPI_WR_DATA(0x55);     // RGB565 / 16 bit
```

Alle anderen Init-Werte bleiben beim ersten Test unverändert.

Der relevante Unterschied lautet also:

```diff
 SPI_WR_REG(0x3A);
-SPI_WR_DATA(0x77);
+SPI_WR_DATA(0x55);
```

Das ist **unsere Anpassung**, nicht Bestandteil des Baidu-Originalcodes.

---

# 7. BGR / MADCTL

Der Hersteller setzt:

```text
36 = 08
```

Für den ersten Test sollte dieser Wert beibehalten werden.

Das kann bedeuten, dass die Farbreihenfolge zunächst als BGR erscheint.

Falls Rot und Blau vertauscht sind, gibt es zwei mögliche Lösungen:

## Softwareseitig

Framebuffer zunächst als BGR565 interpretieren.

Kein zusätzlicher Scanout-Aufwand.

## Später testen

```c
SPI_WR_REG(0x36);
SPI_WR_DATA(0x00);
```

um die BGR-Umschaltung möglicherweise zu deaktivieren.

**Nicht gleichzeitig mit dem ersten RGB565-Bring-up ändern.**

Erst Bild erzeugen, danach Farbkanäle korrigieren.

---

# 8. RGB565-Pinmapping

RGB565 benutzt:

```text
5 Bit Rot
6 Bit Grün
5 Bit Blau
```

Framebuffer-Layout:

```text
bit 15                     bit 0

RRRRR GGGGGG BBBBB
^^^^^ ^^^^^^ ^^^^^
15-11 10-5   4-0
```

Für eine direkte PIO-Ausgabe ist es günstig, 16 fortlaufende RP2350-GPIOs so zu routen:

```text
GPIO base+0  -> B0
GPIO base+1  -> B1
GPIO base+2  -> B2
GPIO base+3  -> B3
GPIO base+4  -> B4

GPIO base+5  -> G0
GPIO base+6  -> G1
GPIO base+7  -> G2
GPIO base+8  -> G3
GPIO base+9  -> G4
GPIO base+10 -> G5

GPIO base+11 -> R0
GPIO base+12 -> R1
GPIO base+13 -> R2
GPIO base+14 -> R3
GPIO base+15 -> R4
```

Dann entspricht ein `uint16_t` direkt dem gewünschten PIO-Pinwort.

PIO kann pro Pixel einfach:

```asm
out pins, 16
```

ausgeben.

Keine:

- LUT
- Farbumrechnung
- Bit-Permutation
- CPU-Beteiligung

erforderlich.

---

# 9. Framebuffergröße

Für 480×854 RGB565:

```text
480 × 854 × 2 Byte
= 819,840 Byte
```

also ungefähr:

```text
800.6 KiB
```

Ein einzelner Framebuffer benötigt somit weniger als 1 MiB der vorhandenen 16-MB-PSRAM.

Double Buffering wäre ebenfalls problemlos möglich:

```text
2 × 819,840
= 1,639,680 Byte
≈ 1.56 MiB
```

---

# 10. Exakte DCLK-Werte für reduzierte Refresh-Raten

Der Hersteller-Testtiming-Satz hat:

```text
HTOTAL = 528
VTOTAL = 897
```

Damit gilt:

```text
DCLK = HTOTAL × VTOTAL × Refresh
```

## 30 Hz

```text
528 × 897 × 30
= 14,208,480 Hz

DCLK = 14.20848 MHz
```

## 25 Hz

```text
528 × 897 × 25
= 11,840,400 Hz

DCLK = 11.84040 MHz
```

## 20 Hz

```text
528 × 897 × 20
= 9,472,320 Hz

DCLK = 9.47232 MHz
```

Zum Vergleich Hersteller:

```text
26.3 MHz
≈ 55.53 Hz
```

---

# 11. RGB565-PSRAM-Bandbreite

Nur die aktiven 480×854 Pixel werden aus dem Framebuffer gelesen.

Framegröße:

```text
819,840 Byte
```

## 30 Hz

```text
819,840 × 30
= 24,595,200 Byte/s
≈ 24.6 MB/s
```

## 25 Hz

```text
819,840 × 25
= 20,496,000 Byte/s
≈ 20.5 MB/s
```

## 20 Hz

```text
819,840 × 20
= 16,396,800 Byte/s
≈ 16.4 MB/s
```

Damit ergeben sich die geplanten Arbeitspunkte:

| Refresh | DCLK | PSRAM Scanout |
|---:|---:|---:|
| Hersteller ~55.5 Hz | 26.30 MHz | ~45.5 MB/s |
| **30 Hz** | **14.208 MHz** | **24.6 MB/s** |
| **25 Hz** | **11.840 MHz** | **20.5 MB/s** |
| **20 Hz** | **9.472 MHz** | **16.4 MB/s** |

---

# 12. Aktueller bevorzugter Arbeitspunkt

Erster Zielwert:

```text
480 × 854
RGB565
30 Hz
DCLK = 14.20848 MHz
Framebuffer = 819,840 Byte
PSRAM Read = 24.6 MB/s
```

Warum zunächst 30 Hz?

- deutlich geringere PSRAM-Last als Herstellerbetrieb
- für eine Bedienoberfläche sehr flüssig
- keine Multimedia-Anforderung
- große Sicherheitsreserve gegenüber 20 Hz bezüglich möglichem Panel-Flicker
- CPU-Scanoutlast bleibt praktisch null

Anschließend testen:

```text
25 Hz
20 Hz
```

Ziel ist festzustellen, wie weit die physische Panel-Refresh-Rate reduziert werden kann, ohne:

- Flicker
- Helligkeitspumpen
- Inversionsartefakte
- Ghosting
- Bildinstabilität

zu erzeugen.

---

# 13. CPU-Architektur

Ziel:

```text
                   +----------------------+
Core 0 ----------> | pTOS / GEM / Apps   |
                   +----------+-----------+
                              |
                              | zeichnet nur bei Änderung
                              v
                     RGB565 Framebuffer
                         in PSRAM
                              |
                              | DMA
                              v
                         PIO TX FIFO
                              |
                              v
                     paralleles RGB565
                              |
                              v
                           ST7701S
                              |
                              v
                            TFT


Core 1:
vollständig unabhängig
für Echtzeitaufgaben
```

Laufender Scanout:

```text
PSRAM -> DMA -> PIO -> ST7701S
```

CPU-Aufwand nach Initialisierung:

```text
Core 0 Scanout: ~0 %
Core 1 Scanout:  0 %
```

Die CPU wird nur benötigt, wenn GEM tatsächlich neue Bildinhalte zeichnet.

---

# 14. PIO-Idee

Ein 32-Bit-DMA-Wort enthält exakt zwei RGB565-Pixel:

```text
31                             0

+---------------+---------------+
| Pixel 1       | Pixel 0       |
| RGB565 16 Bit | RGB565 16 Bit |
+---------------+---------------+
```

PIO Pixel-State-Machine:

```asm
.wrap_target
    out pins, 16
    out pins, 16
.wrap
```

Mit Autopull nach 32 Bit kann DMA kontinuierlich nachfüllen.

In der endgültigen Implementierung müssen DCLK und Pixel-Ausgabe natürlich exakt synchronisiert werden.

Eine Möglichkeit ist ein 2-Takt-Pixelzyklus:

```asm
out pins, 16 side 0
nop          side 1
```

wobei `side` DCLK steuert.

Alternativ können Pixel-Daten und Timing auf getrennte State Machines verteilt werden.

---

# 15. Empfohlene PIO-Aufteilung

Für einen robusten Prototyp bietet sich an:

```text
PIO SM0:
RGB565 Pixelstream

PIO SM1:
DCLK / DE / HSYNC / VSYNC

optional weitere SM:
Frame-/Line-Steuerung oder Synchronisation
```

Wichtig ist:

**Der Pixel-DMA darf nicht dazu führen, dass ein FIFO-Stall den Display-DCLK unkontrolliert verlängert.**

Deshalb sollte die endgültige Architektur entweder:

1. genügend FIFO-/DMA-Reserve besitzen, oder
2. DCLK/Timing unabhängig vom Pixel-FIFO erzeugen.

Der DMA soll ausschließlich aktive Pixel lesen.

Während H/V-Blanking muss kein Framebuffer gelesen werden.

---

# 16. Hersteller-Timing als PIO-Ziel

Externer RGB-Zyklus:

```text
Horizontal:

HSYNC     6
Back      30
Active    480
Front     12

TOTAL     528
```

```text
Vertical:

VSYNC     1
Back      30
Active    854
Front     12

TOTAL     897
```

Polaritäten laut Screenshot:

```text
DCLK:  Rising
VSYNC: Falling
HSYNC: Falling
DE:    Falling
```

Bei der Implementierung muss verifiziert werden, ob „Falling“ im Testtool:

- Active-Low-Signal
- oder Auswahl der Flanke

bedeutet.

Darum:

**Beim ersten Logic-Analyzer-Test die tatsächlich funktionierende Herstellerinterpretation nachbilden.**

---

# 17. Empfohlene Bring-up-Reihenfolge nach Eintreffen der Samples

## Schritt 1 – Hardware prüfen

- FPC-Pinout gegen tatsächliches Sample prüfen
- Versorgung prüfen
- IOVCC/VCC prüfen
- Backlight nicht direkt an 3,3 V anschließen
- korrekten Konstantstrom-/Boost-Treiber für LED-Backlight verwenden

## Schritt 2 – Original-Hersteller-Init

Zunächst:

```text
COLMOD = 0x77
MADCTL = 0x08
26.3 MHz
Hersteller-Porches
```

verwenden.

Ziel:

**Panel im unveränderten Referenzbetrieb zum Leben bringen.**

Noch keine Optimierung.

## Schritt 3 – RGB565

Nur ändern:

```text
3A: 77 -> 55
```

Sonst nichts.

Testpattern:

```text
Rot
Grün
Blau
Weiß
Schwarz
Grau
Farbverlauf
```

## Schritt 4 – Farbkanäle prüfen

Falls R/B vertauscht:

- zunächst Framebuffer-/Pinmapping anpassen
- erst später `MADCTL 0x36` experimentell ändern

## Schritt 5 – 30 Hz

Externe Timings unverändert lassen:

```text
528 × 897
```

nur DCLK reduzieren auf:

```text
14.20848 MHz
```

Beobachten:

- Flicker
- Ghosting
- Helligkeit
- Inversion
- Stabilität
- Touch-Störungen
- DMA-Stabilität

## Schritt 6 – 25 Hz

```text
11.84040 MHz
```

## Schritt 7 – 20 Hz

```text
9.47232 MHz
```

## Schritt 8 – Dauerlauf

Mehrere Stunden:

- statische GUI
- wechselnde GUI
- hohe/geringe Backlight-Helligkeit
- Touch gleichzeitig aktiv
- Core 1 unter Last
- PSRAM unter Last

---

# 18. Was NICHT gleichzeitig verändert werden soll

Beim Bring-up immer nur eine Variable verändern.

Nicht gleichzeitig:

```text
RGB-Modus
MADCTL
Gamma
GIP
Power-Register
Porches
DCLK
```

ändern.

Empfohlene Reihenfolge:

```text
1. Original zum Laufen bringen
2. RGB565
3. Farbkanäle
4. 30 Hz
5. 25 Hz
6. 20 Hz
7. erst danach Feintuning
```

---

# 19. Offene Risiken

## 19.1 Panel-Minimalfrequenz

Der ST7701S verbietet einen langsamen DCLK nicht offensichtlich.

Das bedeutet aber nicht automatisch, dass die LCD-Zelle bei 20 Hz optisch perfekt funktioniert.

Mögliche Symptome:

- Flicker
- Inversion-Flimmern
- Ghosting
- Helligkeitsschwankungen

Darum reale Samples testen.

## 19.2 PSRAM-Buskonkurrenz

RGB565 bei 30 Hz benötigt ca.:

```text
24.6 MB/s
```

dauerhaften Read-Traffic.

Das ist deutlich mehr als der frühere 4-bpp-RGBI-Ansatz.

Zu messen:

- DMA-Unterläufe
- Auswirkungen auf GEM
- Flash/QMI-Arbitration
- Auswirkungen auf Core 0
- Echtzeitverhalten Core 1

## 19.3 Tearing

GEM schreibt in denselben Framebuffer, den DMA gerade scannt.

Mögliche Strategien:

- Single Buffer zunächst akzeptieren
- Dirty Rectangles
- Synchronisation mit VBlank
- später Double Buffering

Double Buffering kostet nur ca. 1,56 MiB PSRAM und ist daher grundsätzlich problemlos.

## 19.4 Backlight

Das Panel-Backlight benötigt eine höhere Spannung als die Logikversorgung.

Nicht direkt an 3,3 V anschließen.

Ein geeigneter LED-Konstantstrom-Boost-Treiber ist erforderlich.

---

# 20. Warum RGB565 jetzt bevorzugt wird

Ursprünglich wurde 4-bpp RGBI favorisiert, weil beim direkten LVDS/HSTX-Ansatz das 7:1-LVDS-Packing sonst CPU-/PIO-seitig sehr teuer geworden wäre.

Beim EP5008S entfällt dieser Engpass:

```text
kein LVDS
kein 7:1 Serializer
kein HSTX
kein RGB-LUT
```

Stattdessen:

```text
RGB565 framebuffer
    |
    v
DMA
    |
    v
PIO
    |
    v
16 parallele RGB-Datenleitungen
    |
    v
ST7701S
```

Damit bleiben die wichtigsten ursprünglichen Ziele erhalten:

- nahezu keine CPU-Last für Scanout
- Core 1 vollständig frei
- einfacher Datenpfad
- schneller Systemstart
- deterministische Architektur

Gleichzeitig steigt die Farbtiefe von:

```text
16 Farben
```

auf:

```text
65.536 Farben
```

---

# 21. Aktuelle Architekturentscheidung

**Bevorzugter Prototyp:**

```text
Display:
EP5008S
ST7701S
480 × 854

Pixelformat:
RGB565
16 bit/pixel
65.536 Farben

Framebuffer:
~801 KiB
in 16-MB-PSRAM

Scanout:
DMA + PIO

Initialisierung:
3-Wire-SPI
Herstellersequenz
COLMOD 0x55

Erster Refresh:
30 Hz

DCLK:
14.20848 MHz

Bandbreite:
24.6 MB/s

Core 0:
GEM / pTOS / Anwendungen

Core 1:
komplett frei für Echtzeit
```

---

# 22. Minimaler Startcode – Konzept

```c
void ep5008s_start(void)
{
    // Hardware reset
    lcd_reset_low();
    delay_ms(20);
    lcd_reset_high();
    delay_ms(120);

    // Originale EP5008S/ST7701S-Registersequenz
    ep5008s_vendor_init();

    // Änderung für GEMbedded:
    lcd_cmd(0x3A);
    lcd_data(0x55);       // RGB565

    // Hersteller-MADCTL zunächst beibehalten
    lcd_cmd(0x36);
    lcd_data(0x08);

    // PIO auf Hersteller-Porches konfigurieren,
    // zunächst z.B. 30 Hz:
    rgb_pio_init(
        480, 854,
        30, 12, 6,        // HBP, HFP, HSYNC
        30, 12, 1,        // VBP, VFP, VSYNC
        14208480           // DCLK
    );

    dma_rgb565_init(framebuffer);

    // Danach autonomer Betrieb:
    dma_start();
    pio_start();
}
```

Nach diesem Punkt:

```text
PSRAM -> DMA -> PIO -> EP5008S
```

und die CPU muss den Display-Refresh nicht mehr bedienen.

---

# 23. Checkliste für den Tag, an dem die Displays ankommen

```text
[ ] FPC-Aufdruck dokumentieren/fotografieren
[ ] FPC-Pinout mit Händlerbild vergleichen
[ ] Versorgung messen
[ ] Backlight-Treiber anschließen
[ ] Reset-Sequenz prüfen
[ ] 3-Wire-SPI mit Logic Analyzer prüfen
[ ] kompletten Hersteller-Init senden
[ ] RGB888 Referenz-Testpattern
[ ] auf RGB565 / 0x55 wechseln
[ ] RGB565 Farbtest
[ ] BGR/RGB prüfen
[ ] 55.5-Hz-Referenzbetrieb stabil?
[ ] 30 Hz testen
[ ] 25 Hz testen
[ ] 20 Hz testen
[ ] DMA-Unterlauf prüfen
[ ] PSRAM-Bandbreite messen
[ ] Core-0-Latenz messen
[ ] Core-1-Echtzeitverhalten unter Scanout messen
[ ] Touch GT911 gleichzeitig testen
[ ] Dauerlauf
```

---

# 24. Wichtigste Merksätze für den Wiedereinstieg

1. **Nicht mehr mit LVDS/HSTX anfangen.** Dieses Panel ist paralleles RGB und daher einfacher.

2. **Originalen Baidu-Hersteller-Init zuerst unverändert zum Laufen bringen.**

3. Danach für RGB565 nur zuerst:

```text
3A = 55
```

statt:

```text
3A = 77
```

setzen.

4. `36 = 08` zunächst beibehalten.

5. Externe Referenztimings:

```text
H: 6 / 30 / 480 / 12
V: 1 / 30 / 854 / 12
```

6. Hersteller-DCLK:

```text
26.3 MHz ≈ 55.5 Hz
```

7. Unser erster Zielwert:

```text
14.20848 MHz = 30 Hz
```

8. Danach 25 Hz und 20 Hz testen.

9. RGB565 benötigt bei 30 Hz:

```text
~24.6 MB/s PSRAM Read
```

10. Zielarchitektur bleibt:

```text
PSRAM -> DMA -> PIO -> ST7701S

Core 0: GEM
Core 1: Echtzeit
```

---

# 25. Quellenbasis dieser Notiz

## Hersteller-/Baidu-Datei

`EP5008S初始化.c`

Enthält:

- originale ST7701S-Initialisierungssequenz
- Bank-Register
- Gamma
- Power/VCOM
- GIP
- Sleep-Out
- Display-On
- Original-COLMOD `0x77`
- Original-MADCTL `0x08`

## Hersteller-Testparameter-Screenshot

Enthält:

- 480 × 854
- 24 Bit
- HBP/HFP/HSPW
- VBP/VFP/VSPW
- Signalpolaritäten
- 26.3 MHz
- S3W
- `JLT5008A_init`

## EP5008S Produktbilder

Enthalten:

- ST7701S
- RGB888/RGB666/RGB565
- GT911
- mechanische Maße
- 40-Pin-RGB-FPC
- separate Touch-Schnittstelle

---

# 26. Zusammenfassung

Der EP5008S ist für GEMbedded derzeit ein sehr attraktiver Kandidat.

Der entscheidende Vorteil gegenüber dem zuvor untersuchten direkten LVDS-Ansatz ist:

> **Das Panel akzeptiert paralleles RGB565 direkt.**

Dadurch entfällt jede aufwendige LVDS-/HSTX-Farbserialisierung.

Die aktuell geplante Lösung ist:

```text
480×854
RGB565
30 Hz
~801 KiB Framebuffer
~24.6 MB/s PSRAM-Read

PSRAM -> DMA -> PIO -> ST7701S
```

Damit sind **65.536 Farben bei praktisch CPU-freiem Scanout** möglich.

Wenn die zwei Samples eingetroffen sind, wird zuerst der unveränderte Herstellerbetrieb validiert. Danach wird schrittweise auf RGB565 und anschließend auf 30/25/20 Hz reduziert.

Der entscheidende praktische Versuch lautet dann:

> **Wie niedrig kann die physische Refresh-Rate des EP5008S abgesenkt werden, ohne sichtbare LCD-Artefakte zu erzeugen?**

Wenn 30 Hz stabil funktionieren, ist die Kombination **EP5008S + RGB565 + RP2350B + PSRAM + DMA/PIO** ein sehr starker Kandidat für die endgültige GEMbedded-Displayarchitektur.
