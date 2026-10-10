# GEMbedded RP2350B Carrier – Rev-A Pinout

**Stand:** 25.09.2026  
**Status:** **ENTWURF — von keiner Platine bestätigt, nicht danach verdrahten.**  
Diese Belegung ist ausgedacht und nie gebaut worden. Sie taugt als Motivation
dafür, wie eng ein 48-Pin-Brett mit 16-Bit-Panel wird, und als nichts weiter.
Verbindlich sind nur Chipfakten (SVD, SDK-Header) und die Pins in den
Treiberquellen; siehe „Herkunft der Zahlen“ in `../BoardRessourcesManager.md`.
Was überlebt hat, steht dort in 13.7.1.  
**Basis:** WeAct Studio RP2350B Core Board mit PSRAM auf der Unterseite

---

## 1. Plattformidee

Das Carrierboard soll als universelle GEMbedded-Plattform dienen.

Geplante Hauptfunktionen:

- WeAct RP2350B Core Board
- PSRAM auf dem vorgesehenen Footprint des WeAct-Boards
- EP5008S 5" Touchdisplay, 480×854
- RGB565 über PIO + DMA
- alternativ HDMI/DVI über HSTX
- microSD
- ESP-01 / ESP8266 mit AT-Firmware
- USB über den vorhandenen USB-Anschluss des WeAct-Boards; bei Bedarf externer USB-Hub
- GT911 Touch über **eigenen 6-poligen FPC-Anschluss**
- LED-Backlight-Boost / Konstantstromtreiber
- Application-/Expansion-I/O
- möglichst viele freie GPIOs für Maschinen-, Robotik- und Steuerungsanwendungen

Wichtig:

> **EP5008S und HDMI/DVI werden alternativ benutzt, nicht gleichzeitig.**

Der 40-polige FPC-Anschluss ist daher ein kombinierter:

> **Display / Expansion Port**

Der 6-polige Touch-FPC bleibt davon getrennt.

---

## 2. Modulkonzept

### Panel-Modus

```text
WeAct RP2350B
      |
      +---- 40P FPC ---- EP5008S
      |                  RGB565 + Timing + LCD-Init
      |
      +---- 6P FPC ----- GT911 Touch
      |
      +---- microSD
      +---- ESP-01
      +---- USB / externer Hub
      +---- Application Header
```

### HDMI-Modus

```text
WeAct RP2350B
      |
      +---- 40P FPC ---- HDMI/DVI-Adapter
      |                  HSTX + zusätzliche GPIO
      |
      +---- 6P Touch-FPC bleibt leer
      |
      +---- microSD
      +---- ESP-01
      +---- USB / externer Hub
      +---- Application Header
```

---

## 3. CPU-Aufteilung

```text
Core 0
------
pTOS
GEM / GEMbedded
Dateisystem
Touch
WiFi
UI
Systemdienste

Core 1
------
Echtzeit
FPU-Berechnungen
Motion Planning
Kinematik
Regelalgorithmen
Signalverarbeitung

PIO / DMA / Timer
-----------------
Display-Scanout
präzise Step-/Dir-Pulse
Encoder
zeitkritische I/O
Streaming
```

Grundprinzip:

> **CPU berechnet – PIO garantiert Timing.**

---

## 4. GPIO-Tabelle – beide Betriebsmodi

| PIN | Funktion | Ziel-Gerät / Zweck – **Panel-Modus** | Ziel-Gerät / Zweck – **HDMI-Modus** |
|---:|---|---|---|
| **GP0** | PSRAM CS | PSRAM auf WeAct-Unterseite | PSRAM auf WeAct-Unterseite |
| **GP1** | RGB565 B0 / GPIO | EP5008S B0 | freie GPIO über HDMI-/Expansion-Adapter |
| **GP2** | RGB565 B1 / GPIO | EP5008S B1 | freie GPIO über HDMI-/Expansion-Adapter |
| **GP3** | RGB565 B2 / GPIO | EP5008S B2 | freie GPIO über HDMI-/Expansion-Adapter |
| **GP4** | RGB565 B3 / GPIO | EP5008S B3 | freie GPIO über HDMI-/Expansion-Adapter |
| **GP5** | RGB565 B4 / GPIO | EP5008S B4 | freie GPIO über HDMI-/Expansion-Adapter |
| **GP6** | RGB565 G0 / GPIO | EP5008S G0 | freie GPIO über HDMI-/Expansion-Adapter |
| **GP7** | RGB565 G1 / GPIO | EP5008S G1 | freie GPIO über HDMI-/Expansion-Adapter |
| **GP8** | RGB565 G2 / GPIO | EP5008S G2 | freie GPIO über HDMI-/Expansion-Adapter |
| **GP9** | RGB565 G3 / GPIO | EP5008S G3 | freie GPIO über HDMI-/Expansion-Adapter |
| **GP10** | RGB565 G4 / GPIO | EP5008S G4 | freie GPIO über HDMI-/Expansion-Adapter |
| **GP11** | RGB565 G5 / GPIO | EP5008S G5 | freie GPIO über HDMI-/Expansion-Adapter |
| **GP12** | RGB565 R0 / HSTX | EP5008S R0 | HSTX / TMDS |
| **GP13** | RGB565 R1 / HSTX | EP5008S R1 | HSTX / TMDS |
| **GP14** | RGB565 R2 / HSTX | EP5008S R2 | HSTX / TMDS |
| **GP15** | RGB565 R3 / HSTX | EP5008S R3 | HSTX / TMDS |
| **GP16** | RGB565 R4 / HSTX | EP5008S R4 | HSTX / TMDS |
| **GP17** | DCLK / HSTX | EP5008S DCLK | HSTX / TMDS |
| **GP18** | HSYNC / HSTX | EP5008S HSYNC | HSTX / TMDS |
| **GP19** | VSYNC / HSTX | EP5008S VSYNC | HSTX / TMDS |
| **GP20** | DE / GPIO | EP5008S DE | freie GPIO über HDMI-/Expansion-Adapter |
| **GP21** | **DISPLAY_MODE_ID** | über unbenutzte RGB-Leitung am 40P-FPC; Pull-down → Panel erkannt | HDMI-Adapter zieht Leitung auf 3V3 → HDMI-Modus erkannt |
| **GP22** | **HDMI HPD / Expansion GPIO** | frei nutzbar als digitale Application-GPIO; am Panel nur an unbenutzter RGB-Leitung | HDMI Hot-Plug-Detect über Pegelanpassung auf dem HDMI-Adapter |
| **GP23** | User Button / GPIO | WeAct Onboard-Taster, optional Anwendung | WeAct Onboard-Taster, optional Anwendung |
| **GP24** | LCD CS / GPIO | ST7701S CS für serielle Initialisierung | freie GPIO über HDMI-/Expansion-Adapter |
| **GP25** | User LED / GPIO | WeAct Onboard-LED, optional Anwendung | WeAct Onboard-LED, optional Anwendung |
| **GP26** | LCD RESET / GPIO | ST7701S Reset | freie GPIO über HDMI-/Expansion-Adapter |
| **GP27** | Backlight PWM | Enable/PWM des WLED-Boost-Treibers | Boost bleibt aus; Pin zunächst für Boost reserviert |
| **GP28** | LCD SDA beim Boot → I²C SDA | ST7701S Init; danach GT911 SDA / System-I²C | freie GPIO bzw. I²C über HDMI-/Expansion-Adapter |
| **GP29** | LCD SCL beim Boot → I²C SCL | ST7701S Init; danach GT911 SCL / System-I²C | freie GPIO bzw. I²C über HDMI-/Expansion-Adapter |
| **GP30** | Touch INT / GPIO | GT911 INT am separaten 6P-Touch-FPC | frei am Carrier/Application Header; Touch-FPC leer |
| **GP31** | Touch RESET / GPIO | GT911 RESET am separaten 6P-Touch-FPC | frei am Carrier/Application Header; Touch-FPC leer |
| **GP32** | SPI0 MISO | microSD + gemeinsamer System-SPI | microSD + gemeinsamer System-SPI |
| **GP33** | SPI0 CS | microSD Chip Select | microSD Chip Select |
| **GP34** | SPI0 SCK | microSD + gemeinsamer System-SPI | microSD + gemeinsamer System-SPI |
| **GP35** | SPI0 MOSI | microSD + gemeinsamer System-SPI | microSD + gemeinsamer System-SPI |
| **GP36** | UART TX | ESP-01 RX / AT-Firmware | ESP-01 RX / AT-Firmware |
| **GP37** | UART RX | ESP-01 TX / AT-Firmware | ESP-01 TX / AT-Firmware |
| **GP38** | Expansion GPIO | frei; GPIO/PWM/PIO/weiterer CS | frei; GPIO/PWM/PIO/weiterer CS |
| **GP39** | Expansion GPIO | frei; GPIO/PWM/PIO/weiterer CS | frei; GPIO/PWM/PIO/weiterer CS |
| **GP40** | ADC0 / Expansion | frei; analog/digital | frei; analog/digital |
| **GP41** | ADC1 / Expansion | frei; analog/digital | frei; analog/digital |
| **GP42** | ADC2 / Expansion | frei; analog/digital | frei; analog/digital |
| **GP43** | ADC3 / Expansion | frei; analog/digital | frei; analog/digital |
| **GP44** | ADC4 / Expansion | frei; analog/digital | frei; analog/digital |
| **GP45** | ADC5 / Expansion | frei; analog/digital | frei; analog/digital |
| **GP46** | ADC6 / Expansion | frei; analog/digital | frei; analog/digital |
| **GP47** | ADC7 / Expansion | frei; analog/digital | frei; analog/digital |

---

## 5. Getrennter Touch-Anschluss

Der GT911-Touch verwendet einen eigenen **6-poligen FPC-Anschluss**.

Belegung funktional:

```text
GND
RESET  -> GP31
INT    -> GP30
SDA    -> GP28
SCL    -> GP29
VCC    -> 3V3
```

Im Panel-Modus:

```text
6P FPC eingesteckt
GT911 aktiv
```

Im HDMI-Modus:

```text
6P FPC leer
GP30/31 dadurch frei
GP28/29 können als I²C/GPIO anderweitig genutzt werden
```

Der Touch-FPC wird **nicht** auf den HDMI-Adapter weitergeführt.

---

## 6. Gemeinsamer I²C-Bus

```text
GP28 = SDA
GP29 = SCL
```

Im Panel-Modus werden diese beiden Pins beim Boot zunächst für die serielle ST7701S-Initialisierung verwendet.

Danach:

```text
LCD CS inaktiv
GP28/29 auf I²C umschalten
GT911 aus Reset holen
```

Anschließend können am I²C-Bus zusätzlich externe Sensoren oder Erweiterungen betrieben werden.

---

## 7. Gemeinsamer SPI0-Bus

```text
GP32 = MISO
GP34 = SCK
GP35 = MOSI
```

microSD:

```text
GP33 = CS
```

Weitere SPI-Geräte teilen:

```text
MISO
MOSI
SCK
```

und benötigen jeweils nur ein eigenes CS.

Beispiele:

```text
ADC
DAC
CAN-Controller
Ethernet-Controller
Sensor
Motorcontroller
```

Voraussetzung:

> Jedes SPI-Gerät muss bei inaktivem CS seinen MISO-Ausgang hochohmig schalten.

---

## 8. UART / WiFi

```text
GP36 = TX
GP37 = RX
```

Verwendung:

```text
ESP-01 / ESP8266
AT-Firmware
```

Der ESP dient als Kommunikationsmodul, nicht als zweite Haupt-CPU.

---

## 9. Freie GPIOs im Panel-Modus

`GP21` ist jetzt fest als `DISPLAY_MODE_ID` reserviert.

Im Panel-Modus frei:

```text
GP22
GP38
GP39
GP40
GP41
GP42
GP43
GP44
GP45
GP46
GP47
```

Das sind:

```text
11 freie Application-GPIO
```

`GP22` liegt zwar zusätzlich auf einer im RGB565-Modus unbenutzten RGB-Leitung des 40P-FPC, kann im Panel-Modus aber als digitale Application-GPIO verwendet werden.

Zusätzlich optional nutzbar:

```text
GP23 = Onboard-Taster
GP25 = Onboard-LED
```

Damit stehen im Panel-Modus praktisch bis zu:

```text
13 Application-I/Os
```

zur Verfügung.

---

## 10. Freie GPIOs im HDMI-Modus

Zusätzlich zum normalen Application-Bereich werden durch Wegfall des Panels frei:

```text
GP1 ... GP11
GP20
GP24
GP26
GP28
GP29
GP30
GP31
```

GP12–GP19 bleiben für HSTX belegt.

Zusätzlich fest belegt:

```text
GP21 = DISPLAY_MODE_ID
GP22 = HDMI HPD
```

GP27 bleibt in Rev A zunächst als Backlight-Boost-Steuerung reserviert, auch wenn der Boost im HDMI-Modus deaktiviert ist.

Damit stellt die HDMI-Variante weiterhin erheblich mehr Application-I/O bereit. Ein großer Teil davon soll auf der HDMI-/Expansion-Adapterplatine direkt herausgeführt werden.

---

## 11. EP5008S-Modus

Display:

```text
EP5008S
ST7701S
480 × 854
RGB565
65.536 Farben
```

Displaybus:

```text
GP1 ... GP16 = RGB565
GP17         = DCLK
GP18         = HSYNC
GP19         = VSYNC
GP20         = DE
```

Framebuffer:

```text
480 × 854 × 2 Byte
= 819,840 Byte
≈ 800.6 KiB
```

Geplanter erster Arbeitspunkt:

```text
30 Hz
DCLK ≈ 14.20848 MHz
```

Scanout:

```text
PSRAM -> DMA -> PIO -> EP5008S
```

---

## 12. HDMI/DVI-Modus

HSTX:

```text
GP12 ... GP19
```

Zielmodus zunächst:

```text
640 × 480
RGB332
8 bit/pixel
256 Farben
```

Framebuffer:

```text
640 × 480 × 1 Byte
= 307,200 Byte
= 300 KiB
```

Der HDMI/DVI-Ausgang sitzt **nicht direkt auf dem Carrier**, sondern auf einer separaten Adapterplatine am 40-poligen Display-/Expansion-FPC.

---

## 13. HDMI-Adapter als GPIO-Expansion

Prinzip:

```text
40P FPC
   |
   +---- GP12...GP19 -> HSTX/TMDS -> HDMI
   |
   +---- GP1...GP11  -> freie Application-GPIO
   |
   +---- GP20        -> freie GPIO
   |
   +---- GP24/26     -> freie GPIO
   |
   +---- GP28/29     -> GPIO / I²C / optional DDC-EDID
   |
   +---- GP21        -> DISPLAY_MODE_ID = HIGH
   |
   +---- GP22        -> HDMI HPD (mit Pegelanpassung)
   |
   +---- Versorgung / GND
```

Damit dient die HDMI-Platine gleichzeitig als:

> **Video-Adapter + Expansion-Board**

Vorteile:

- kein Umlöten
- kein High-Speed-Multiplexer
- keine TMDS-Stubs auf dem Carrier
- gleiche Basisplatine für Panel und Monitor
- zusätzliche GPIO werden im HDMI-Modus direkt zugänglich

---

## 14. Automatische Erkennung des Video-Modus

Dieselbe Firmware soll automatisch erkennen, welches Modul am 40-poligen Display-/Expansion-Port steckt.

Dafür wird ein dedizierter:

```text
DISPLAY_MODE_ID = GP21
```

vorgesehen.

Vorgeschlagene elektrische Logik:

```text
Carrier:
GP21 / DISPLAY_MODE_ID
 |
 +---- 10 kΩ Pull-down nach GND
 |
 +---- über eine im RGB565-Modus unbenutzte RGB-Leitung zum 40P-FPC
```

### Panel-Modus

Das EP5008S verbindet diese zusätzliche RGB-Leitung nur mit einem im RGB565-Modus unbenutzten Display-Eingang.

Dadurch bleibt:

```text
DISPLAY_MODE_ID = LOW
```

und die Firmware erkennt:

```text
PANEL MODE
```

### HDMI-Modus

Der HDMI-Adapter verbindet dieselbe Leitung mit 3V3.

Dadurch gilt:

```text
DISPLAY_MODE_ID = HIGH
```

und die Firmware erkennt:

```text
HDMI MODE
```

Damit ist keine Benutzerwahl und kein Umlöten notwendig.

Boot-Ablauf:

```text
RESET
  |
  v
gemeinsam benutzte Video-Pins zunächst hochohmig
  |
  v
DISPLAY_MODE_ID lesen
  |
  +---- LOW  ----> EP5008S
  |                ST7701S initialisieren
  |                RGB565 / PIO starten
  |                GT911 starten
  |                Backlight-Boost aktivieren
  |
  +---- HIGH ----> HDMI/DVI
                   Backlight-Boost bleibt AUS
                   HSTX auf GP12...GP19 konfigurieren
                   HDMI-Ausgabe starten
```

### HDMI Hot Plug Detect

Zusätzlich wird:

```text
HDMI_HPD = GP22
```

vorgesehen.

Auf dem HDMI-Adapter wird das HPD-Signal des HDMI-Steckers auf 3,3-V-kompatibles Niveau angepasst.

Damit kann die Software unterscheiden:

```text
MODE_ID = LOW
    -> Panel-Modus

MODE_ID = HIGH, HPD = LOW
    -> HDMI-Adapter vorhanden, kein Monitor

MODE_ID = HIGH, HPD = HIGH
    -> HDMI-Adapter und Monitor vorhanden
```

Im Panel-Modus kann GP22 als normale digitale Application-GPIO verwendet werden.

### Optional: DDC / EDID

Im HDMI-Modus können GP28/GP29, da kein GT911 angeschlossen ist, optional als I²C für HDMI-DDC/EDID verwendet werden.

Damit kann die Software später Informationen des Monitors lesen und ggf. geeignete Videomodi auswählen.

### Software-Override

Normalbetrieb:

```ini
VIDEO=AUTO
```

Für Entwicklung und Diagnose optional:

```ini
VIDEO=PANEL
```

oder:

```ini
VIDEO=HDMI
```

Damit bleibt automatische Erkennung der Standardfall, ein Modus kann aber gezielt erzwungen werden.

---

## 15. 40-poliger Display-/Expansion-Port

Arbeitsbezeichnung:

> **Display / Expansion FPC 40P**

Mögliche Module:

- EP5008S
- HDMI/DVI-Adapter
- anderes Display
- Maschinen-I/O-Adapter
- ADC/DAC-Modul
- CAN/Ethernet-Modul
- kundenspezifisches HMI

Der Anschluss transportiert im Panel-Modus:

```text
RGB565
DCLK
HSYNC
VSYNC
DE
ST7701S Init
LED+ / LED-
Versorgung
GND
```

und im HDMI-Modus:

```text
HSTX
freie GPIO
I²C/GPIO
Versorgung
GND
```

---

## 16. Backlight-Boost

Das EP5008S-Backlight benötigt ungefähr:

```text
~18–20 V
```

aus der 5-V-Versorgung.

Dafür wird auf dem Carrier ein kleiner WLED-Boost-/Konstantstromtreiber vorgesehen.

Prinzip:

```text
5 V
 |
 +--> WLED Boost / Constant Current Driver
          |
          +--> LED+
          +--> LED-
               |
               +--> über 40P FPC zum EP5008S
```

Steuerung:

```text
GP27 = PWM / Enable
```

Im HDMI-Modus:

```text
Boost OFF
```

Die LED-Leitungen des 40P-FPC werden auf dem HDMI-Adapter nicht verwendet.

---

## 17. Application-Port

Der besonders wertvolle obere GPIO-Bereich bleibt möglichst frei:

```text
GP40 = ADC0
GP41 = ADC1
GP42 = ADC2
GP43 = ADC3
GP44 = ADC4
GP45 = ADC5
GP46 = ADC6
GP47 = ADC7
```

Zusätzlich sind je nach Pinmux digitale Funktionen möglich:

- SPI
- I²C
- UART
- PWM
- PIO
- normale GPIO
- Interrupts

Empfohlener Header:

```text
3V3   GND
GP40  GP41
GP42  GP43
GP44  GP45
GP46  GP47
GP21  GP22
GP38  GP39
```

---

## 18. USB

Der vorhandene USB-Anschluss des WeAct-Boards wird genutzt.

```text
WeAct USB
   |
   +---- externer USB-Hub
          |
          +---- Tastatur
          +---- Maus
          +---- weitere Peripherie
```

Damit werden keine zusätzlichen GPIO für USB verbraucht.

---

## 19. Carrierboard-Komplexität

Das Carrierboard bleibt bewusst einfach.

Aktive Funktion auf dem Carrier:

```text
WLED Backlight Boost / Konstantstromtreiber
```

Ansonsten überwiegend:

```text
Routing
Steckverbinder
microSD-Sockel
ESP-01-Sockel
FPC-Anschlüsse
Application Header
passive Beschaltung
Abblockung
optionale Serien-/0-Ohm-Widerstände
```

---

## 20. Vorläufig eingefrorene Rev-A-Entscheidungen

```text
GP0
PSRAM CS

GP1–GP16
RGB565 Daten

GP17–GP20
Display Timing

GP12–GP19
alternativ HSTX

GP21
DISPLAY_MODE_ID

GP22
HDMI HPD im HDMI-Modus / digitale Application-GPIO im Panel-Modus

GP24
ST7701S CS

GP26
ST7701S RESET

GP27
Backlight PWM / Enable

GP28/29
ST7701S Init beim Boot
danach I²C0 / GT911

GP30/31
GT911 INT / RESET über separaten 6P-FPC

GP32/34/35
gemeinsamer SPI0

GP33
microSD CS

GP36/37
ESP-01 UART

GP40–GP47
Application-/ADC-Bereich möglichst frei
```

---

## 21. Nächster PCB-Designschritt

Als Nächstes festlegen:

> **exakte physische Pinbelegung des 40-poligen Display-/Expansion-FPC**

Dabei müssen die Original-Pinpositionen des EP5008S berücksichtigt werden.

Danach:

1. 6P-Touch-FPC festlegen
2. HDMI-Adapter-Pinout festlegen
3. WLED-Boost-Schaltung dimensionieren
4. Application-Header festlegen
5. Schaltplan und PCB-Layout beginnen
