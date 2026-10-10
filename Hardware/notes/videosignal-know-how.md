# Videosignal-Know-How

**Stand:** 04.10.2026
**Zweck:** Wie ein Bild aus einem RP2350 herauskommt — und warum die digitalen Wege so viel aufwendiger sind als die analogen.
**Status:** Erklärungen und Rechnungen. Nichts davon ist implementiert.
**Entschieden:** HDMI läuft über **HSTX** (9.). DVI über PIO ist geprüft und verworfen (4.5).

Verwandte Dokumente:

- `../BoardRessourcesManager.md` — Beschreibungssprache, `DEFINE DISPLAY(...)`, Ressourcenampel
- `GEMbedded_EP5008S_ST7701S_RGB565_Bringup_Notes.md`
- `GEMbedded_RP2350B_Carrier_RevA_Pinout V2.md`
- `ladder.py` — rechnet die VGA-Widerstandsleiter aus (Abschnitt 7)
- `tmds_neutral.py` — zählt die TMDS-Symbole, die man ohne Zustand wiederholen darf (Abschnitt 4.5)

---

# 1. Vier Wege, vier Welten

| Weg                    | Prinzip                                       | Pins  | CPU-Last     | Aufwand |
| ---------------------- | --------------------------------------------- | -----:| ------------ | ------- |
| **RGB-Panel parallel** | rohe Bits + Takt, Panel auf derselben Platine | 20+   | ~0           | mittel  |
| **VGA**                | analoge Spannung über Widerstandsleiter       | 11–14 | ~0           | klein   |
| **Composite**          | ein analoges Signal, Sync eingebettet         | ~6    | ~0           | klein   |
| **DVI/HDMI über HSTX** | TMDS-Kodierer in Silizium                     | 8     | ~0           | klein   |
| ~~DVI/HDMI über PIO~~  | TMDS-Symbole, in Software kodiert             | 8     | ein Kern — mit Schattenpuffer wenig (4.5) | **verworfen** |

Der Sprung in der letzten Spalte ist der Gegenstand dieses Dokuments. Dieselbe Aufgabe — ein Bild auf einen Schirm — kostet je nach Weg zwei Größenordnungen Unterschied. **Und der ganze Unterschied steckt in der Frage, ob das Signal das Gehäuse verlässt.**

---

# 2. Warum digitale Videoleitungen überhaupt kodieren

## 2.1 Das Problem

250 Millionen Bit pro Sekunde durch ein zwei Meter langes Billigkabel, das neben einem Schaltnetzteil liegt, zwischen zwei Geräten an zwei verschiedenen Steckdosen.

## 2.2 Zwei Drähte statt einem

Jede Leitung ist ein Paar; beide tragen dasselbe Signal, eines verkehrt herum. Der Empfänger sieht nur die **Differenz**. Störungen treffen beide Drähte gleich und fallen bei der Subtraktion weg.

## 2.3 Stufe 1 — möglichst wenige Flanken

Nicht die Bits kosten, sondern die **Wechsel**: jede Flanke strahlt ab und wird vom Kabel verschliffen. Halb so viele Wechsel heißt ungefähr halb so viel Hochfrequenzenergie — und das entscheidet, ob fünf Meter Kabel noch gehen oder nur einer.

Daher der Name: **T**ransition **M**inimized **D**ifferential **S**ignaling.

Die Umrechnung ist eine Kette, in der jedes Ausgangsbit am vorhergehenden hängt:

```
q0 = d0                   (das erste hat keinen Vorgänger)
q1 = q0 XOR d1
q2 = q1 XOR d2
...
```

Damit wird aus einem Wert ein Wechsel:

| XOR-Betrieb |                    | XNOR-Betrieb |                    |
| ----------- | ------------------ | ------------ | ------------------ |
| Datenbit 1  | Ausgang **kippt**  | Datenbit 1   | Ausgang **bleibt** |
| Datenbit 0  | Ausgang **bleibt** | Datenbit 0   | Ausgang **kippt**  |

Also: Flanken = Zahl der Einsen (XOR) bzw. Zahl der Nullen (XNOR). Der Kodierer **zählt darum zuerst die Einsen** und wählt die günstigere Variante. Weil ein Byte entweder höchstens vier Einsen oder höchstens vier Nullen hat, liefert immer eine der beiden **höchstens vier Flanken**. Das ist die Garantie, um die es geht.

Beispiel, Byte `0xB7`, von rechts gelesen:

```
d  = 1 1 1 0 1 1 0 1        sechs Einsen -> XNOR

q0 = 1
q1: d=1 -> bleibt    1
q2: d=1 -> bleibt    1
q3: d=0 -> kippt     0
q4: d=1 -> bleibt    0
q5: d=1 -> bleibt    0
q6: d=0 -> kippt     1
q7: d=1 -> bleibt    1

q  = 1 1 1 0 0 0 1 1        zwei Flanken statt sechs
```

Das Verfahren ist uralt und heißt NRZI: *schick nicht den Wert, schick die Änderung.* **USB macht exakt dasselbe mit umgekehrtem Ziel** — USB hat keine Taktleitung und *braucht* Flanken zur Taktrückgewinnung, fügt sogar künstlich welche ein. DVI hat eine Taktleitung und will das Gegenteil. Gleiches Werkzeug, entgegengesetztes Ziel, und der Unterschied ist nur, ob ein Taktdraht danebenliegt.

## 2.4 Stufe 2 — der Gleichanteil

Im Empfänger sitzt ein **Kondensator** in der Leitung. Absicht: so müssen sich die Geräte nicht über Massepotentiale einigen.

Ein Kondensator überträgt aber nur Änderungen. Tausend Einsen hintereinander laden ihn auf, der Pegel wandert zur Mitte, und der Empfänger kann oben und unten nicht mehr unterscheiden. Das Bild zerfällt.

Also führt der Sender **Buch**: ein Zähler merkt sich den bisherigen Überschuss an Einsen, und bei jedem Symbol entscheidet er, ob er es normal oder invertiert sendet, um den Zähler Richtung null zu drücken.

> **Dieser laufende Zähler ist der Grund, warum man ein Bild nicht einfach vorkodieren kann.** Er ist kein Designfehler, sondern eine Regelschleife gegen einen Kondensator.

**Aber er lebt nur eine Zeile lang.** Sobald DE in die Austastlücke fällt, wird er auf null gesetzt:

```
wenn DE == 0:   Steuersymbol senden;  cnt = 0
```

Jede Zeile beginnt also bei null, und eine kodierte Zeile hängt **ausschließlich an ihren eigenen Pixeln**. Eine einmal kodierte Zeile bleibt gültig, solange sich ihre Pixel nicht ändern. Darauf baut der Schattenpuffer in Abschnitt 4.5.

## 2.5 Warum zehn Bit

Weil man Auswahl braucht: 256 Eingangswerte, 1024 mögliche Symbole. Jeder Wert bekommt zwei Darstellungen, und es bleibt Platz für vier Sondersymbole, die nie wie Bilddaten aussehen können.

```
 Bit  9    8    7 6 5 4 3 2 1 0
      ^    ^    ^^^^^^^^^^^^^^^
      |    |    die acht Datenbits, ggf. invertiert
      |    "ich habe XOR benutzt" / "ich habe XNOR benutzt"
      "ich habe gerade umgepolt"
```

**Die Gegenseite muss also nichts erraten — es steht dabei.** Entschlüsseln ist stur geradeaus:

```
1. Bit 9 == 1 ?  ->  Bits 0..7 invertieren
2. Bit 8 liest   ->  die XOR- bzw. XNOR-Kette rückwärts rechnen
3. fertig
```

**Der Sender braucht Gedächtnis, der Empfänger keines.** Der ganze Zustand sitzt auf der Senderseite — Monitore werden millionenfach gebaut, die Schwierigkeit wurde bewusst dorthin gelegt, wo sie einmal bezahlt wird. Für uns heißt das: wir stehen auf der schwierigen Seite.

## 2.6 Woher der Empfänger weiß, wo ein Symbol anfängt

Im Bitstrom steht keine Markierung. Verrutscht man um drei Stellen, kommt plausibel aussehender Unsinn heraus.

Dafür ist die **Austastlücke** da. Dort werden vier feste Sondersymbole gesendet, die absichtlich **deutlich mehr Wechsel enthalten, als ein Datensymbol je haben kann** — Stufe 1 sorgt ja gerade dafür, dass Bilddaten flankenarm sind. Flankenreich kann also nur ein Steuersymbol sein. Der Empfänger schiebt sein Zehnerfenster durch den Strom, bis die Muster sauber hineinfallen; dann sitzt das Raster.

**Die Austastlücke ist keine verschenkte Zeit, sondern das Metronom** — in jeder Zeile neu.

## 2.7 Was das für Bildinhalte bedeutet

Weiß heißt `0xFF`, acht Einsen, also XNOR, also „alle bleiben":

```
q = 1 1 1 1 1 1 1 1        null Flanken
```

Stufe 1 macht aus Weiß eine glatte Leitung. Dafür ist die Bilanz schief, und der Kodierer wechselt zwischen den beiden Formen. Ab `cnt = 0` beginnt er mit der invertierten:

```
0000000001 1111111100 0000000001 1111111100 ...     (in Sendereihenfolge)
   -8         +6         -8         +6
```

**Zwei Flanken auf zwanzig Bit** — fast ein sauberes Rechteck. Aber nicht ganz ausgeglichen: die Formen haben −8 und +6, jedes Paar also **−2**. Der Kodierer gleicht das gelegentlich mit einer zweiten gleichen Form aus. Ruhig bleibt es trotzdem: eine weiße Fläche ist das **ruhigste** Signal, das DVI erzeugen kann.

Genau dieses −2 ist übrigens der Grund, warum man Weiß nicht zustandslos wiederholen darf (Abschnitt 4.5).

Das lauteste ist mittleres Grau mit feiner Struktur, etwa `0x55`: vier Einsen, vier Nullen, in jeder Betriebsart vier Flanken — das Maximum. Daraus folgt etwas Messbares: **ein Monitor, der eine einfarbige Fläche zeigt, strahlt weniger und zieht weniger Strom als einer mit feingerastertem Bild.**

## 2.8 Die Kausalkette

```
beliebige Geräte gefahrlos verbinden können
     -> Kondensator in der Leitung
     -> kein absoluter Nullpunkt mehr
     -> Gleichanteil muss null bleiben
     -> laufender Bilanzzähler im Sender
     -> Kodierung ist zustandsbehaftet
     -> man kann nicht einfach vorkodieren
     -> es kostet einen Prozessorkern
```

(Die letzten beiden Glieder lassen sich aufbrechen — mit zeilenweiser Zwischenspeicherung und neutralen Symbolen, Abschnitt 4.5.)

Unser Problem ganz am Ende ist der Preis einer Entscheidung ganz am Anfang, die mit Bildern nichts zu tun hat: dass zwei Geräte an verschiedenen Steckdosen hängen dürfen, deren Massepotentiale sich um etliche Volt unterscheiden können.

## 2.9 Ein Familienmerkmal

Überall, wo zwei eigenständig versorgte Geräte per Kabel reden, findet sich dasselbe: Ethernet (Übertrager, 4B5B/8B10B), S/PDIF, SATA, PCIe, USB 3. Immer Kondensator oder Übertrager, immer eine Kodierung, die den Gleichanteil wegräumt.

**Glasfaser hilft dagegen nicht** — im Gegenteil. Ein optischer Empfänger ist ebenfalls wechselspannungsgekoppelt und hat eine Verstärkungsregelung, die sich auf den mittleren Lichtpegel einstellt; eine lange Folge von Einsen bedeutet Dauerlicht, die Regelung fährt herunter, das Auge geht zu. Und weil keine Taktleitung mitläuft, braucht es dort zusätzlich *genügend* Flanken. Darum trägt jeder optische Standard dieselbe Codefamilie: 8b/10b, 64b/66b, 128b/132b.

Optisch ersetzt ist das kurze Kabel trotzdem nicht, und zwar wegen Strom (aktive Elektronik an beiden Enden, die aus den 5 V des Steckers lebt — daher sind optische HDMI-Kabel gerichtet), wegen Dreck und Mikrometertoleranzen an den Steckern, und weil HDMI nebenbei DDC, CEC, Hotplug und 5 V führt, die keine Faser überträgt.

Die Reise geht ohnehin woandershin: **weniger und schnellere Kupferleitungen mit besserer Mathematik.** HDMI 2.1 hat TMDS durch FRL mit 16b/18b abgelöst, DisplayPort nimmt 128b/132b — der Kodier-Mehraufwand sank von 25 % auf 3 %, bezahlt mit Kodierern, die niemand mehr in PIO nachbauen könnte.

---

# 3. HSTX gegen PIO

## 3.1 Zwei grundsätzliche Unterschiede

**Der TMDS-Kodierer in Hardware.** HSTX hat drei davon in Silizium. PIO kann nicht rechnen — PIO kann nur ausschieben, was jemand vorbereitet hat. Also muss ein Prozessorkern kodieren.

**Zwei Bit pro Takt statt einem.**

```
640x480p60:  Pixeltakt 25,175 MHz  x 10 Bit  =  251,75 Mbit/s je Leitung

PIO:   1 Bit pro Takt  ->  braucht ~252 MHz Systemtakt
HSTX:  2 Bit pro Takt  ->  braucht ~126 MHz Systemtakt
```

HSTX taktet auf beiden Flanken aus. Darum beginnt jede PicoDVI-Portierung mit einer kräftigen Übertaktung weit über die spezifizierten 150 MHz.

## 3.2 Drei Bequemlichkeiten

- **Kommando-Expander**: Austastlücken kosten im DMA-Strom fast keine Daten („sende diesen Wert N-mal").
- **Bit-Umsortierer**: 16-Bit-Pixel landen direkt auf den acht Ausgangsbits.
- **Eigene Taktdomäne und eigenes FIFO.**

## 3.3 Was PIO sehr wohl kann

Das **Differenzpaar**. Die Pad-Steuerung des RP2350 kann einen Ausgang invertieren (`OUTOVER`), eine State Machine treibt also beide Pins eines Paares gegenphasig. Das ist nicht das Problem.

---

# 4. Warum man ein Bild nicht vorkodieren kann

Das Wort versteckt das Problem: **es kann kein *vor* geben, nur ein *gerade eben rechtzeitig*.**

## 4.1 Der Datenweg

```
Framebuffer RGB565
      |  5/6/5 -> drei Kanalwerte
      v
TMDS-Kodierer in Software, 3x, zustandsbehaftet
      |
      v
drei Symbolpuffer, je 10 Bit pro Pixel
      |  3x DMA
      v
drei PIO-SMs -- je 1 Bit/Takt auf ein Pinpaar (N über OUTOVER invertiert)
      +
vierte SM: Takt, festes Muster 1111100000, frei laufend
```

Drei getrennte State Machines statt einer — sonst müsste man die Bits **transponieren**, also für jeden Bitzeitpunkt das Byte bauen, das alle Pins gleichzeitig treibt. Mit je einer SM pro Leitung entfällt das. Der Takt braucht keinen Puffer, er ist ein konstantes Muster.

## 4.2 Ein ganzes Bild geht nicht

```
10 Bit x 3 Kanäle x 800 Pixelplätze  =  3 KB je Zeile
x 525 Zeilen                          =  1,575 MB je Bild
x 60 Hz                               =  94,5 MB/s Lesebandbreite
```

Über dem, was der QMI liefert — und jede Bildänderung macht den Puffer ungültig.

## 4.3 Also: eine Zeile voraus, für immer

Zwei SRAM-Puffer zu je 3 KB; während der DMA Zeile *N* ausschiebt, kodiert der Kern Zeile *N+1*. Der Speicher ist lächerlich, das Tempo nicht:

```
25,175 Mio. Pixel/s  x  3 Kanäle  =  ~75 Mio. Kodierungen pro Sekunde
```

Vier Kniffe machen das möglich, und alle sind nötig:

1. **Nachschlagetabellen statt Rechnen.** Bei RGB565 sind die Kanalwerte 5 bzw. 6 Bit breit — Tabellen mit 32 und 64 Einträgen.
2. **Zustand loswerden.** Ein Paar gleicher Pixel landet beim Standardkodierer für die meisten Werte bei ±2, nicht bei null (nachgerechnet in 4.5). Zustandslos wird es erst, wenn man sich auf Symbole beschränkt, die *selbst* neutral sind. Preis: leicht geringere Farbtreue.
3. **Assembler-Innenschleife**, auf die Doppelausführung des Cortex-M33 verschränkt.
4. **Pixelverdopplung.** Zwei gleiche Ausgangspixel teilen sich ein Symbol — **halbiert die Kodierarbeit.** Darum ist 320×240 auf 640×480 der realistische Modus und nicht bloß ein Sparmodus.

```
640x480 nativ,   RGB565:  614.400 B x 60  =  36,9 MB/s Framebuffer-Read
320x240 verdoppelt:       153.600 B x 60  =   9,2 MB/s
```

Der Framebuffer-Verkehr wäre damit geringer als beim heutigen SPI-Display. Teuer bleibt allein das Kodieren, und das hängt an der **Ausgangs**auflösung.

## 4.4 Die Asymmetrie, die am meisten weh tut

Beim dummen RGB-Panel kauft man sich Luft, indem man die **Bildrate senkt**: 55 → 30 → 20 Hz, DCLK von 26,3 auf 9,5 MHz. **Bei HDMI gibt es diesen Hebel nicht.** Ein Monitor rastet nur auf Standardmodi ein, und 640×480p60 ist der Boden.

HDMI ist alles oder nichts, wo das Panel ein Dimmer ist.

## 4.5 Der kodierte Schattenpuffer — es geht doch

> **Entschieden am 04.10.2026: verworfen.** Nicht weil es nicht ginge — dieser
> Abschnitt zeigt, dass es geht —, sondern weil der Preis in keinem Verhältnis
> steht: 252 MHz Systemtakt, sieben von zwölf State Machines, zehn bis zwölf
> von sechzehn DMA-Kanälen und ~5 Bit Farbtiefe je Kanal, für etwas, das
> HSTX nebenbei erledigt. **Für HDMI bleiben wir bei HSTX.**
>
> Der Abschnitt bleibt als Beleg stehen: die Frage ist beantwortet, und sie
> muss nicht noch einmal gestellt werden. Interessant bleibt er auch
> technisch — die Zeilenunabhängigkeit (2.4) und die neutralen Symbole gelten
> unabhängig davon, wer serialisiert.

### Die Idee

1. pTOS/GEM ist alleiniger Herr des Bildschirms und weiß, wo gezeichnet wurde.
2. Speicher ist genug da.
3. Man hält eine **fertig TMDS-kodierte Kopie** des Bildschirms.
4. Ein Bit-Array markiert die Zeilen, die neu gezeichnet wurden (Mausbewegung eingeschlossen).
5. Eine Task kodiert **nur diese Zeilen** neu.
6. PIO und DMA schieben die Kopie unverändert hinaus.

Die Voraussetzung ist erfüllt, weil der Bilanzzähler pro Zeile auf null fällt (2.4): eine kodierte Zeile hängt nur an ihren eigenen Pixeln.

Und die Kodierarbeit bricht tatsächlich zusammen:

```
eine Zeile 640 Pixel x 3 Kanäle   =  1920 Kodierungen
mit Tabellen, ~20 Takte je Stück  =  ~38.000 Takte  =  0,26 ms bei 150 MHz
```

Eine Teilzeit-Task schafft gut sechzig schmutzige Zeilen pro Bild. Bei ruhendem Bild fallen **null** Kodierungen an statt 75 Millionen pro Sekunde.

### Die Wand dahinter: das Auslesen

Die Idee beseitigt die Kodierseite. Die Ausleseseite bleibt:

```
aktive Zeile: 640 px x 10 Bit x 3 Kanäle = 19.200 Bit = 2400 Byte
              als 30-Bit-Worte gepackt   = 2560 Byte
x 480 Zeilen = 1,17 MB je Bild
x 60 Hz      = 73,7 MB/s, dauerhaft, aus dem PSRAM
```

QSPI-PSRAM liefert 4 Bit je Takt: bei 75 MHz QSPI-Takt **~37 MB/s roh**, abzüglich Kommando-Overhead und abzüglich dessen, was der XIP-Code aus dem Flash **auf demselben Bus** holt. **Bei 640×480 nativ fehlt ungefähr der Faktor zwei.**

Die Austastung muss dabei nicht mitgespeichert werden — sie ist konstant, ein verketteter DMA wiederholt sie aus einem winzigen SRAM-Puffer. Das steckt in den 73,7 MB/s schon drin.

### Was der Serialisierer beitragen kann — und was nicht

Bei DVI über PIO gibt **jeder Befehl genau ein Bit aus**, bei 252 MHz. Ein `mov`, ein `jmp`, ein `pull` kostet einen Takt ohne Bit, und dann reißt der Strom. Rechnen oder entpacken kann diese State Machine nicht. Gratis sind nur:

- **Autopull mit Schwelle 30**: drei Symbole pro 32-Bit-Wort, Nachladen ohne Takt, 6 % Verschnitt statt 37 % bei „10 Bit in 16"
- **Wrap**: die Schleife springt ohne Takt zurück
- **Taktleitung**: eine eigene, frei laufende SM mit festem Muster, ganz ohne Speicher

Das ist das Optimum fürs Ausschieben. Verkleinern muss jemand anderes.

### Wiederholen darf man nur neutrale Symbole

Naheliegend: jedes Symbol einmal speichern, doppelt ausgeben (320×240 → 640×480). Ob das erlaubt ist, entscheidet der Gleichanteil. Ausgezählt über alle 256 Werte mit `tmds_neutral.py`:

```
Standardkodierer, gleicher Wert zweimal ab cnt=0:
   endet wieder bei 0:     52 von 256 Werten
   endet bei ±2:          204 von 256       <- driftet bei jeder Wiederholung weiter

Werte, deren Symbol (in einer der zwei Formen) selbst 5 Einsen / 5 Nullen hat:
   100 von 256,  Bereich 8 .. 246,  größte Lücke 10
   Schwarz 0x00: Formen -8 / +10    -> nicht dabei
   Weiß    0xFF: Formen +6 / -8     -> nicht dabei
```

Allgemein: ein Paar aus Normal- und invertierter Form hat **immer ±2**, nie null. Null gibt es nur, wenn ein Symbol schon allein ausgeglichen ist.

Ein **neutrales** Symbol darf man beliebig oft und in beliebiger Reihenfolge wiederholen — die Bilanz bleibt null, das Symbol hängt nur noch vom Pixelwert ab, **ohne jeden Zustand**.

Preis: **Schwarz wird 8, Weiß wird 246** (je ~3 % daneben), und die Lücken bis 10 entsprechen etwa 5 Bit pro Kanal. Für RGB565 und eine GEM-Oberfläche verkraftbar; ein LCD zeigt bei 8 ohnehin kein echtes Schwarz.

### Die Verdoppler-SM

Mit nur neutralen Symbolen genügt eine zweite State Machine pro Leitung, zwischen Speicher und Serialisierer:

```
.wrap_target
    out x, 10      ; ein Symbol aus dem kompakten Strom (Autopull bei 30)
    in  x, 10      ; einmal ...
    in  x, 10      ; ... und nochmal (Autopush bei 20)
.wrap
```

Drei Takte pro Quellpixel bei zwanzig verfügbaren — viel Luft. (Bitreihenfolge von OUT und IN noch passend einzustellen.) Ein **chipinterner** DMA reicht das Ergebnis an den Serialisierer; das PSRAM sieht davon nichts.

Die **vertikale** Verdopplung macht der DMA: jede Zeile einmal aus dem PSRAM in einen SRAM-Zeilenpuffer, von dort zweimal ausgespielt.

### Was dabei herauskommt

```
GEM-Framebuffer   320x240 RGB565, 154 KB im SRAM   <- genau der, den es heute gibt
TMDS-Schatten     240 Zeilen x 1284 B = 308 KB im PSRAM
PSRAM-Lesen       18,5 MB/s   (mit SRAM-Zeilenpuffer)
                  37 MB/s     (ohne, jede Zeile zweimal aus dem PSRAM)
Ausgabe           640x480p60 DVI
Kodieren          nur schmutzige Zeilen: 960 Nachschlagen ≈ 40 µs je Zeile
                  ganzes Bild neu ≈ 10 ms -- weniger als ein Frame
```

Aus „fehlt Faktor zwei" wird **„passt mit Faktor zwei bis drei Reserve"**. Und die VDI zeichnet weiter ins schnelle SRAM — der HDMI-Ausgang wird ein zweiter Abnehmer des bestehenden Bildschirms, wie heute der SPI-Treiber.

### Ein Loch und sein Flicken

Ein Atari-Programm darf sich mit `Physbase()` die Bildschirmadresse holen und direkt hineinschreiben — viele tun das. Solche Zugriffe markieren keine Zeile. Auffangnetz: ein kompletter Neudurchlauf alle paar Bilder; bei ~10 ms je Durchlauf billig.

### 32 Bit pro Pixel?

Für den **kodierten** Schatten ja — drei Symbole je Wort, saubere 32-Bit-DMA-Transfers. Für den **GEM-Framebuffer** nein: dort verdoppelt es jeden VDI-Zugriff, und der DMA hat mit 16 Bit keine Mühe.

### Was es kostet

- **252 MHz Systemtakt** — nicht verhandelbar, der Serialisierer braucht ein Bit pro Takt. Dafür steigt der PSRAM-Takt mit (Teiler 2 → 126 MHz), und genau das macht die Bandbreite erst bequem.
- **7 von 12 State Machines** (3 Serialisierer, 3 Verdoppler, 1 Takt)
- **~10–12 von 16 DMA-Kanälen** mit Zeilenpuffer, 6–7 ohne — der eigentlich knappe Posten, ein Fall für die Ressourcenampel
- **Farbquantisierung** wie oben

### Was vorher gemessen werden muss

1. Wie viel PSRAM-Bandbreite bei 126 MHz QSPI **dauerhaft** ankommt, während XIP-Code mitliest. 18,5 MB/s sind sicher, 37 MB/s nicht.
2. Ob das Brett 252 MHz mit PSRAM stabil hält.
3. Ob ein echter Monitor das Bild annimmt.

*(Hinfällig, seit der Weg verworfen ist — notiert für den Fall, dass die Frage je wieder aufkommt.)*

### Die Dirty-Line-Verwaltung lohnt sich ohnehin

Unabhängig von DVI:

1. **SPI-Display, Teilaktualisierung** — die SPI-Leitung ist die langsamste Strecke im System; nur geänderte Zeilen zu schicken vervielfacht dort die Bildrate. Größter Effekt pro Aufwand.
2. **Zeichnen im PSRAM** (RGB-Panel) — nichts wird unnötig gelesen oder geschrieben.

---

# 5. Composite

Composite weiß auch nicht, wo null ist — es löst es nur anders, und ehrlich gesagt eleganter: **es schickt den Nullpunkt jede Zeile mit.** Sync-Puls und Schwarzschulter sind ein bekannter Referenzpegel, 15.625-mal pro Sekunde geliefert, und im Empfänger hält sich eine Klemmschaltung jedes Mal neu daran fest. Der Farbträger macht es nochmal: acht Schwingungen Burst auf der Schwarzschulter, damit die Phase stimmt.

**Die Austastlücke von DVI ist die Urenkelin der Schwarzschulter.** Dasselbe Prinzip, nur mit Symbolen statt Spannungen.

Für GEMbedded wäre Composite ein überraschend guter Ausgang: eine PIO-State-Machine, ein Widerstandsnetzwerk aus wenigen Bauteilen, DMA aus dem Framebuffer. Kein Kodierer, keine Bilanz, kein Taktrückgewinnungsproblem, keine Lizenz. 320×240 ist ungefähr genau das, was eine Bildröhre zeigen kann — die Auflösung passt ohne Skalierung. Und es wäre zeitlich stimmig: ein µST am Fernseher von 1985.

---

# 6. VGA

## 6.1 Warum es für uns der interessanteste Weg ist

```
RGB-Panel:  16 Pins -> digitale Pegel      -> Panel
VGA:         n Pins -> Widerstandsleiter   -> 0..0,7 V an 75 Ohm -> Monitor
```

Dieselbe PIO-Struktur: eine State Machine erzeugt das Timing, eine schiebt Pixel, DMA holt sie aus dem Framebuffer, CPU-Last null. **Der einzige Unterschied ist die Ausgangsstufe.** Wer den RGB-parallel-Scanout baut, bekommt VGA fast geschenkt.

## 6.2 Keine Kodierung

HSYNC und VSYNC laufen auf **eigenen Drähten**. Masse ist gemeinsam, das Kabel kurz, und der Schwarzpegel wird — wie bei Composite — auf der Schwarzschulter jeder Zeile neu geklemmt. Der Bezug wird mitgeschickt statt aus der Statistik rekonstruiert. Damit fallen **beide** Stufen aus Abschnitt 2 weg.

```
640x480@60:  Pixeltakt 25,175 MHz
             -> 150 MHz / 6 = 25,0 MHz   (0,7 % daneben, unkritisch)
             -> PIO gibt EIN Sample pro Pixeltakt aus, nicht 10 Bit x 3
```

## 6.3 Pinkosten im Vergleich

```
Composite       ~6   (Leiter, Sync im Signal)
DVI/PIO          8
VGA 3-3-3       11   (9 Farbe + HSYNC + VSYNC)
VGA 4-4-4       14
RGB565-Panel    20+
```

VGA ist **billiger an Pins als das Panel** — auf einem nackten Brett das knappste Gut.

## 6.4 Eine hübsche Fügung

Die ST-Hochauflösung war 640×400 bei 71,2 Hz. VGA kennt 640×400 bei 70 Hz als echten Modus — **praktisch deckungsgleich.** Ein µST, das sein Originalbild ohne Skalierung an einen VGA-Monitor liefert.

## 6.5 Der Haken

Es ist analog: die Leiter muss stimmen, 75 Ω wollen eingehalten sein, eine schlampige Leitung zeigt Schatten. Dafür verzeiht ein VGA-Monitor fast jedes Timing, das ungefähr passt — er rastet ein, wo HDMI schwarz bleibt.

## 6.6 Das Breakout

Drei Widerstandsleitern, zwei Schutzwiderstände, eine Buchse. Unter einem Euro Material.

- **Sync**: direkt vom GPIO auf die Buchse, je ~100 Ω in Reihe gegen Überschwingen. 3,3 V reichen, der Monitor hat TTL-Eingänge. PIO kann die beiden über **Side-Set** treiben — nebenbei zur Pixelausgabe, ohne eine zusätzliche Instruktion.
- **DDC mitnehmen**: an Pin 12 und 15 liegen SDA und SCL, darüber meldet der Monitor sein EDID — welche Modi er kann, welche Frequenzen, welche Größe. Zwei GPIOs mehr, und das Gerät muss nicht beschrieben werden, sondern **sagt selbst, was es ist**.
- **Buchse** DE-15: Pin 1/2/3 = R/G/B, 5–8 und 10 Masse, 13/14 Sync, 12/15 DDC.
- **Pinwahl**: die Farbbits müssen **zusammenhängen**, damit PIO sie mit einem `out pins, 9` ausgibt. Und die Beschreibung muss sagen, welcher Pin das höchstwertige Bit trägt — sonst sind Hell und Dunkel vertauscht.

```
DEFINE DISPLAY(#0, TYPE=VGA, RES=640x480, REFRESH=60,
                   R=GP20..GP22, G=GP23..GP25, B=GP26..GP28,  -- LSB zuerst
                   HSYNC=GP29, VSYNC=GP30,
                   DDC=(SCL=GP31, SDA=GP32));
```

Für den allerersten Versuch braucht es kein Breakout: die neun Widerstände direkt in den Stecker eines alten VGA-Kabels löten. Hässlich, aber in zwanzig Minuten fertig — und man weiß sofort, ob Timing und Bitreihenfolge stimmen.

---

# 7. Die Leiter ausrechnen

## 7.1 Zwei Bedingungen

**1 — Gewichtung.** Jedes Bit speist über seinen Widerstand Strom in einen gemeinsamen Knoten. Damit Bit *i* doppelt so viel beiträgt wie Bit *i−1*, muss sich der **Leitwert** verdoppeln, der Widerstand also halbieren:

```
R_i = R_LSB / 2^i          der MSB hat den kleinsten Widerstand
```

**2 — Vollausschlag.** Im Monitor liegen fest **75 Ω gegen Masse**. Sind alle Bits oben, liegen alle Zweige parallel (= R_s) und bilden mit den 75 Ω einen Teiler. Gefordert sind 0,7 V aus 3,3 V:

```
R_s = R_L · (V_GPIO / V_FS − 1) = 75 · (3,3/0,7 − 1) = 278,6 Ω
```

Und weil R_s die Parallelschaltung aller n Zweige ist:

```
R_s = R_LSB / (2^n − 1)     ⟹     R_LSB = 278,6 Ω · (2^n − 1)
```

Damit ist die ganze Leiter bestimmt. Drei Zeilen, kein Probieren.

## 7.2 Warum sie exakt linear ist

Man erwartet eine krumme Kennlinie, weil sich der Teiler mit jedem zugeschalteten Bit ändert. Tut er aber nicht — **weil ein GPIO, das eine Null ausgibt, aktiv auf Masse zieht und nicht hochohmig wird.** Jeder Zweig hängt immer an irgendeinem Pegel.

Also ist der Gesamtleitwert am Knoten **konstant**:

```
V = V_GPIO · (Σ Leitwerte der Einsen) / (Σ aller Leitwerte + 1/R_L)
```

Nenner fix, Zähler proportional zum Zahlenwert — **perfekt linear.** Würde man ein Bit stattdessen auf Eingang schalten, wäre die Kennlinie sofort krumm. Das ist kein Detail, sondern der Grund, warum die Schaltung taugt.

## 7.3 Die Werte

Gerechnet mit `ladder.py`, Pad-Widerstand 40 Ω abgezogen, auf E96 gerundet:

|           | MSB |      |      |      | LSB  | Vollausschlag | Stufenfehler | Strom MSB |
| --------- | --- | ---- | ---- | ---- | ---- | ------------- | ------------ | --------- |
| **3 Bit** | 442 | 931  | 1910 |      |      | 0,704 V       | 2,9 %        | 5,4 mA    |
| **4 Bit** | 487 | 1000 | 2050 | 4120 |      | 0,698 V       | 9,0 %        | 4,9 mA    |
| **5 Bit** | 499 | 1050 | 2100 | 4320 | 8660 | 0,699 V       | 9,2 %        | 4,8 mA    |

Der Stufenfehler ist in Prozent **einer Stufe** angegeben — also harmlos; monoton bleibt es weit darunter.

## 7.4 Drei Korrekturen, die in der Praxis zählen

**Der Pad-Widerstand gehört abgezogen.** Das Pad des RP2350 hat je nach Treiberstärke einige zehn Ohm Innenwiderstand, und der liegt in Reihe mit *jedem* Leiterwiderstand. Beim LSB (8,6 kΩ) belanglos, beim MSB (500 Ω) sind es 8 %. Die 40 Ω in `ladder.py` sind eine **Annahme** — einmal nachmessen und neu rechnen.

**Treiberstärke am MSB hochsetzen.** Der MSB-Pin liefert rund **5 mA**. Steht er auf der schwächsten Stufe, bricht der Pegel ein und die hellste Stufe ist zu dunkel.

**Alles hängt am 3,3-V-Pegel**, und der ist keine Konstante. Darum ist VGA-Weiß nie ganz exakt, und zwei Bretter zeigen leicht verschiedene Farben.

## 7.5 Wo die Grenze liegt

Nicht bei der Toleranz — 1-%-Widerstände tragen rechnerisch bis etwa sieben Bit. Die praktische Grenze ist die **Wertespreizung**, bei n Bit gleich 2^(n−1):

```
4 Bit   8:1     bequem
5 Bit  16:1     noch gut
6 Bit  32:1     LSB bei 17 kΩ, trägt noch 1,6 % -- unangenehm
8 Bit 128:1     unsinnig
```

Darüber nimmt man eine **R-2R-Leiter**: nur zwei Widerstandswerte, beliebig viele Bit, Ausgangswiderstand codeunabhängig. Mehr Bauteile, dafür kein Wertezoo.

Für uns: **4 oder 5 Bit je Farbe als gewichtete Leiter** — 4096 oder 32768 Farben, zwölf bis fünfzehn Widerstände.

## 7.6 Anpassung — ehrlich dazu

Die Quelle hat 279 Ω statt der 75 Ω des Kabels, ist also **nicht angepasst**. An einem kurzen Kabel sieht man das nicht, an drei Metern vielleicht als leichten Schatten. Jedes Bastelprojekt macht es so, und es funktioniert — es ist ein Kompromiss, kein Design. Richtig wäre ein Treiberbaustein.

## 7.7 Nachprüfen ohne Monitor

75 Ω gegen Masse ans Ende, die Codes 0…2ⁿ−1 durchzählen und messen. Die Treppe muss gleichmäßig sein und oben bei 0,7 V ankommen. Weicht eine einzelne Stufe ab, ist genau dieser Widerstand oder dessen Pad schuld — man sieht sofort, welcher.

## 7.8 `ladder.py`

Liegt neben diesem Dokument und ist direkt lauffähig (`python ladder.py`).

```python
VG = 3.3        # GPIO-Pegel
VFS = 0.7       # Vollausschlag am Monitor
RL = 75.0       # Abschluss im Monitor
RPAD = 40.0     # Ausgangswiderstand des Pads (Annahme)

E96 = [100, 102, 105, 107, 110, 113, 115, 118, 121, 124, 127, 130, 133, 137,
       140, 143, 147, 150, 154, 158, 162, 165, 169, 174, 178, 182, 187, 191,
       196, 200, 205, 210, 215, 221, 226, 232, 237, 243, 249, 255, 261, 267,
       274, 280, 287, 294, 301, 309, 316, 324, 332, 340, 348, 357, 365, 374,
       383, 392, 402, 412, 422, 432, 442, 453, 464, 475, 487, 499, 511, 523,
       536, 549, 562, 576, 590, 604, 619, 634, 649, 665, 681, 698, 715, 732,
       750, 768, 787, 806, 825, 845, 866, 887, 909, 931, 953, 976]


def e96(x):
    best, dec = None, 1.0
    while x / dec >= 1000:
        dec *= 10
    while x / dec < 100:
        dec /= 10
    for v in E96:
        c = v * dec
        if best is None or abs(c - x) < abs(best - x):
            best = c
    return best


RS = RL * (VG / VFS - 1)
print("geforderter Quellwiderstand R_s = %.1f Ohm" % RS)
print()

for n in (3, 4, 5):
    R0 = RS * (2 ** n - 1)                 # LSB-Widerstand
    ideal = [R0 / (2 ** i) for i in range(n)]       # i=0 LSB ... i=n-1 MSB
    corr = [r - RPAD for r in ideal]
    chosen = [e96(r) for r in corr]

    print("%d Bit je Farbe   (%d Farben bei drei Kanaelen)" % (n, (2 ** n) ** 3))
    print("   Bit   ideal      minus Pad    E96")
    for i in range(n - 1, -1, -1):
        tag = "MSB" if i == n - 1 else ("LSB" if i == 0 else "b%d " % i)
        print("   %-4s %8.1f   %8.1f    %6.0f" % (tag, ideal[i], corr[i], chosen[i]))

    # Nachrechnen: tatsaechliche Kennlinie mit den gewaehlten Werten
    g = [1.0 / (chosen[i] + RPAD) for i in range(n)]
    gtot = sum(g) + 1.0 / RL
    steps = []
    for code in range(2 ** n):
        gon = sum(g[i] for i in range(n) if code & (1 << i))
        steps.append(VG * gon / gtot)
    fs = steps[-1]
    lsbv = fs / (2 ** n - 1)
    dnl = max(abs((steps[k + 1] - steps[k]) / lsbv - 1) for k in range(2 ** n - 1))
    imsb = (VG - fs) / (chosen[n - 1] + RPAD)
    print("   -> Vollausschlag %.3f V   max. Stufenfehler %.1f %%   Strom MSB %.1f mA"
          % (fs, dnl * 100, imsb * 1000))
    print()


```

Andere Randbedingungen — anderer Pegel, anderer Abschluss, gemessener Pad-Widerstand — einfach oben ändern.

Ausgabe:
geforderter Quellwiderstand R_s = 278.6 Ohm

`3 Bit je Farbe   (512 Farben bei drei Kanaelen)
   Bit   ideal      minus Pad    E96
   MSB     487.5      447.5       442
   b1      975.0      935.0       931
   LSB    1950.0     1910.0      1910
   -> Vollausschlag 0.704 V   max. Stufenfehler 2.9 %   Strom MSB 5.4 mA`

`4 Bit je Farbe   (4096 Farben bei drei Kanaelen)
   Bit   ideal      minus Pad    E96
   MSB     522.3      482.3       487
   b2     1044.6     1004.6      1000
   b1     2089.3     2049.3      2050
   LSB    4178.6     4138.6      4120
   -> Vollausschlag 0.698 V   max. Stufenfehler 9.0 %   Strom MSB 4.9 mA`

`5 Bit je Farbe   (32768 Farben bei drei Kanaelen)
   Bit   ideal      minus Pad    E96
   MSB     539.7      499.7       499
   b3     1079.5     1039.5      1050
   b2     2158.9     2118.9      2100
   b1     4317.9     4277.9      4320
   LSB    8635.7     8595.7      8660
   -> Vollausschlag 0.699 V   max. Stufenfehler 9.2 %   Strom MSB 4.8 mA`

---

# 8. Der HDMI-Anschluss des Waveshare RP2350-PiZero

Aus dem Schaltplan (`Waveshare/RP2350PIzeroSchaltplan.jpg`):

```
GPIO32  DVI_D2_P     GPIO38  DVI_CLK_P      GPIO46  DVI_CEC
GPIO33  DVI_D2_N     GPIO39  DVI_CLK_N      GPIO45  DVI_SCL
GPIO34  DVI_D1_P                            GPIO44  DVI_SDA
GPIO35  DVI_D1_N
GPIO36  DVI_D0_P
GPIO37  DVI_D0_N
```

Je **200 Ω in Reihe** vor jeder Leitung, dazu der Hinweis *„impedance should be set at 100 ohms"*. Das ist **kein TMDS-Sendebaustein**, sondern das PicoDVI-Widerstandsverfahren: zwei GPIOs treiben gegenphasig, die Widerstände machen daraus einen Pseudo-Differenzpegel. Ein GPIO ist ein grober 3,3-V-Schalter, TMDS erwartet etwa ein halbes Volt — die Widerstände bilden mit dem Abschluss im Monitor den Teiler.

Die Paarreihenfolge ist **D2, D1, D0, CLK** — Takt zuletzt. Das kann man nicht raten, es muss aufgeschrieben werden.

**HSTX kann hier nicht helfen:** der Block liegt fest auf GP12–19. Das Brett hat ihn nicht benutzt.

**GP32–39 sind in pTOS heute frei.** Belegt sind 0/1 (Konsole), 4/5 (ESP), 6–9 und 20 (Touch), 10/11/21/23/24/25 (LCD), 30/31/40/43 (SD), 47 (PSRAM).

Benutzbar ist dieser Stecker damit trotzdem nicht: er bräuchte den PIO-Weg, und der ist verworfen (4.5). **Der Waveshare bleibt ein Brett ohne HDMI.** Die Verdrahtung steht hier als Dokumentation des Bretts, nicht als Bauanleitung:

```
-- verworfen, siehe 4.5; nur zur Dokumentation des Bretts
DEFINE DISPLAY(#1, TYPE=PIODVI, RES=640x480, REFRESH=60, FORMAT=RGB565,
                   D2=GP32/GP33, D1=GP34/GP35, D0=GP36/GP37, CLK=GP38/GP39,
                   DDC=(SCL=GP45, SDA=GP44), CEC=GP46, CFG=NONE);
```

## 8.1 Warum der Waveshare kein Panel-Ziel sein kann

Das Brett belegt alles oberhalb GP27 selbst:

```
GP28/29   PIO-USB
GP30/31   SD SCK/MOSI        GP40..43  SD MISO / SDIO D1,D2,D3+CS
GP32..39  DVI (4 Paare)      GP44..46  DVI SDA/SCL/CEC
GP47      PSRAM CS
```

An die 40-polige Leiste kommen nur **GP0–27**, also 28 Pins. Ein RGB565-Panel braucht 16 zusammenhängende Datenleitungen plus DCLK, HSYNC, VSYNC, DE, `_CS`, SCL, SDA, RESET, BL = **25**. Blieben drei — kein ESP, kein Touch, nichts für den Anwender.

**Darum das WeAct RP2350B als Panel-Ziel.**

---

# 9. HDMI über HSTX anschließen

Das ist der beschlossene Weg. Auf dem WeAct RP2350B fehlt nur die Buchse.

## 9.1 Die Widerstände braucht man auch mit HSTX

HSTX liefert den Kodierer und die Geschwindigkeit, **aber keinen echten TMDS-Treiber.** Elektrisch ist es derselbe Trick wie beim PIO-Weg: zwei gegenphasige CMOS-Ausgänge über Vorwiderstände. Die fertigen HSTX-DVI-Breakouts machen es genauso.

Der Monitor terminiert jede Leitung mit **50 Ω gegen seine eigenen 3,3 V**. Der Pin zieht über den Vorwiderstand nach Masse, und der Hub ergibt sich aus dem Teiler — wobei der Pin seine eigenen ~40 Ω mitbringt:

```
Hub = 3,3 V − 3,3 V × (R+40) / (R+40+50)

R = 200 Ω  (Waveshare)  ->  570 mV   etwas kräftig
R = 220 Ω               ->  533 mV   fast exakt
R = 270 Ω  (klassisch)  ->  458 mV   etwas schwach
```

TMDS will nominal **500 mV**. **220 Ω ist von den drei üblichen Werten der beste.**

## 9.2 Pins — und eine Freiheit, die der Waveshare nicht hat

GP12–19, das steht fest. Die Paare müssen **benachbart** sein (12/13, 14/15, 16/17, 18/19), weil der zweite Pin die invertierte Hälfte liefert.

Welches Paar dann Takt, D0, D1 oder D2 wird, ist aber **frei**: HSTX kann seine Ausgangsbits umsortieren. Anders als beim Waveshare, wo die Leiterbahnen entschieden haben (8.), darf hier gelötet werden, wie es mechanisch bequem ist — die Zuordnung kommt hinterher in die Boardbeschreibung, und die Firmware stellt sich darauf ein.

```
DEFINE DISPLAY(#0, TYPE=HSTX, RES=640x480, REFRESH=60, FORMAT=RGB565,
                   CLK = GP12/GP13,        -- je Paar: plus/minus
                   D0  = GP14/GP15,
                   D1  = GP16/GP17,
                   D2  = GP18/GP19,
                   DDC = (SCL=.., SDA=..), CFG = NONE);
```

## 9.3 Besser eine Buchse auf Breakout als ein aufgeschnittenes Kabel

Ein aufgeschnittenes HDMI-Kabel ist unangenehmer, als es aussieht: neunzehn Adern, nicht beschriftet, die vier TMDS-Paare als einzeln geschirmte verdrillte Pärchen mit Beidraht, der Rest dünn und lose. Das auseinanderzumessen kostet mehr Zeit als die ganze Lösung.

Eine **HDMI-Buchse auf Breakout** kostet wenige Euro, hat beschriftete Pads und die Masseflächen schon an der richtigen Stelle. Dann lötet man acht Widerstände dazwischen und ist fertig.

Falls es doch das Kabel wird: die vier geschirmten Pärchen sind an der Bauform erkennbar, und **jeder Schirm gehört an Masse, möglichst dicht an den Widerständen**.

## 9.4 Drei Kleinigkeiten, die sonst Zeit kosten

**Pin 18 auf +5 V legen.** Daraus speist der Monitor seinen EDID-Baustein. Ohne die 5 V antwortet DDC nicht — und dann weiß man nicht, ob die Verdrahtung oder das Timing schuld ist.

**Stichleitungen kurz halten.** Der Waveshare-Schaltplan schreibt es selbst hin: *„The trace length here should be kept as short as possible, as the frequency is high."* Handverdrahtet gibt es keinen definierten Wellenwiderstand — also wenige Zentimeter, Paare verdrillt lassen, kurzes Monitorkabel, 640×480.

**DDC ist optional.** Für einen festen Modus braucht es die beiden Leitungen nicht. Nimmt man sie mit, sagt der Monitor hinterher selbst, was er kann — das Prinzip „fragen statt beschreiben" aus `../BoardRessourcesManager.md`.

## 9.5 Der DVI Sock — das fertige Modul

Winzige Platine mit HDMI-Buchse und den acht Widerständen drauf (`HDMI-Miniboard.jpg`). Ursprünglich der **Pico DVI Sock** von Luke Wren, von Adafruit nachgebaut; auf AliExpress als Klon zu haben.

Er passt nicht zufällig: Raspberry Pi hat die HSTX-Pins des RP2350 **absichtlich** auf dieselben GPIOs gelegt, die PicoDVI schon benutzte, damit dieses Zubehör weiterläuft. Adafruit bestätigt es ausdrücklich — *„The DVI Sock uses the HSTX pins on the Pico 2."*

### Die Lane-Reihenfolge

```
GP12  D0+      GP16  D2+
GP13  D0−      GP17  D2−
GP14  CK+      GP18  D1+
GP15  CK−      GP19  D1−
```

**D0, CK, D2, D1** — nicht aufsteigend, nicht naheliegend, und genau der Fall, für den die Beschreibung existiert. HSTX sortiert seine Ausgangsbits passend um.

```
DEFINE DISPLAY(#0, TYPE=HSTX, RES=640x480, REFRESH=60, FORMAT=RGB565,
                   D0  = GP12/GP13,
                   CLK = GP14/GP15,
                   D2  = GP16/GP17,
                   D1  = GP18/GP19,
                   CFG = NONE);
```

### Stückliste und Lizenz

```
1x  Stewart SS-53000-001        HDMI-Buchse, Typ A
8x  0402, 270 Ω                 Original; Adafruit und die Klone: 220 Ω
1x  2-poliger Header, optional  +5 V zum Monitor
```

**CC0-1.0**, also gemeinfrei, 1 mm Platine, auf JLCPCB-Zweilagen ausgelegt. Das Modul ist damit eine Zwischenlösung: **sobald es ein GEMbedded-Carrier gibt, wandern Buchse und acht Widerstände einfach dorthin** — kein Zukauf, keine Drähte, keine Lizenzfrage. Für eine eigene Platine dann **220 Ω**, mit der Begründung aus 9.1.

### Zwei Fallstricke

**Das 5-V-Pad ist herausgeführt, aber nicht angeschlossen.** Adafruit: *„technically you're supposed to provide 5 V to the DVI port, it isn't convenient to do so on the Sock so it's left disconnected."* Beim Pico egal, bei uns an Drähten genauso leicht mitzuverdrahten.

> **Fehlersuche:** Bleibt der Monitor schwarz, ist das Erste **5 V auf dieses Pad** — nicht am Timing drehen. Manche Geräte ziehen ohne die Spannung ihr Hotplug-Signal nicht hoch.

**Der Autor warnt vor genau unserem Aufbau.** Im README steht für den Steckbrettbetrieb: *„cut off the last 5 pins on each side"* — gegen parasitäre Verbindungen an den schnellen Videosignalen. Der Entwurf setzt voraus, dass die Socke **flach aufliegt**, mit praktisch null Leitungslänge. Jeder Draht zum WeAct ist genau dieser Effekt, nur freiwillig.

Also: **drei bis fünf Zentimeter, nicht fünfzehn**, jedes Paar für sich verdrillt (P mit N, nicht alle acht zusammen), **beide Massepads** benutzt und die Massen zwischen den Paaren geführt. Zeigt das Bild Zittern oder Pixelmüll: erst die Drahtlänge verdächtigen, dann alles andere.

DDC ist nicht herausgeführt — **kein EDID**, der Modus steht fest. Für 640×480 in Ordnung, aber das Gegenteil von „fragen statt beschreiben".

### Doku

- `https://github.com/Wren6991/Pico-DVI-Sock` — Originalentwurf, KiCad, Schaltplan-PDF, Gerber, CC0
- `https://learn.adafruit.com/adafruit-dvi-sock-for-pico` — Pinbelegung, 5-V-Hinweis, Pico-2-Kompatibilität

## 9.6 Was es im Pinhaushalt bedeutet

GP12–19 waren in der geprüften WeAct-Belegung genau der freigehaltene Block. **Panel und HDMI passen also beide aufs Brett** — danach bleiben vier Pins (GP2, 3, 6, 7).

Zwei Bildschirmausgänge, zwischen denen man umschaltet, nicht zwei gleichzeitig: GEM kennt einen Schirm.

---

# 10. Folgerungen für GEMbedded

1. **Den parallelen Scanout generisch bauen**, nicht als EP5008S-Treiber. RGB-Panel und VGA teilen sich fast den ganzen Code; der Unterschied ist die Ausgangsstufe.
2. **Reihenfolge nach Aufwand pro Nutzen:**
   
   ```
   1. RGB-Panel   das Ziel, eingebaut, beste Qualität
   2. HDMI/HSTX   Hardware-Kodierer, acht Pins, acht Widerstände (9.)
   3. VGA         fast derselbe Treiber wie das Panel, extern, fast gratis dazu
   4. Composite   wenige Widerstände, nostalgisch, niedrige Auflösung
   -- DVI/PIO     geprüft und verworfen (4.5)
   ```
3. **HDMI heißt HSTX.** DVI über PIO ist technisch möglich (4.5), steht aber in keinem Verhältnis: 252 MHz, sieben State Machines, zwölf DMA-Kanäle, ~5 Bit Farbtiefe — für etwas, das ein Hardwareblock nebenbei erledigt. Ein Brett ohne HSTX bekommt eben kein HDMI; es hat VGA und Composite.
4. **Fruit Jam und WeAct können beide HDMI**, der Waveshare nicht (sein Stecker hängt an GP32–39, nicht an HSTX). Das WeAct bleibt Panel-Ziel (8.1) und bekommt die HDMI-Buchse nach 9. dazu; der Waveshare bleibt Testbrett für alles, was keine acht freien HSTX-Pins braucht.
5. **Dirty-Line-Verwaltung bauen** — sie zahlt sich beim SPI-Display und beim Zeichnen im PSRAM sofort aus, unabhängig davon, welcher Ausgang kommt. Von der PIO-DVI-Untersuchung ist das der Teil, der bleibt.

---

# 11. Merksätze

1. **Der Unterschied zwischen einfach und schwierig ist, ob das Signal das Gehäuse verlässt.**
2. **Nicht Bits kosten, sondern Flanken.** Darum die XOR/XNOR-Kette.
3. **Der laufende Bilanzzähler existiert, weil ein Kondensator in der Leitung sitzt** — und der sitzt dort, damit man Geräte gefahrlos verbinden kann.
4. **Der Sender braucht Gedächtnis, der Empfänger keines.** Bit 8 und Bit 9 sagen alles.
5. **Die Austastlücke ist das Metronom**, nicht verschenkte Zeit — und die Urenkelin der Schwarzschulter.
6. **Eine weiße Fläche ist das ruhigste Signal, feines Grau das lauteste.**
7. **Vorkodieren geht zeilenweise**, weil der Bilanzzähler in jeder Austastlücke auf null fällt. Ein ganzes Bild nativ scheitert am PSRAM-Bus, nicht am Rechnen.
8. **Bei HDMI fehlt der Bildraten-Hebel.** 640×480p60 ist der Boden.
9. **Glasfaser löst nichts davon** — sie braucht dieselben Codes und zusätzlich Flanken.
10. **Die Widerstandsleiter ist linear, weil Nullen aktiv auf Masse gezogen werden.**
11. **Die Grenze der gewichteten Leiter ist die Wertespreizung**, nicht die Toleranz.
12. **VGA ist fast derselbe Treiber wie das RGB-Panel** — und billiger an Pins.
13. **Der PIO-Serialisierer hat bei DVI keinen freien Takt.** Entpacken muss eine zweite SM.
14. **Wiederholen darf man nur neutrale Symbole** — 100 von 256, Schwarz und Weiß nicht darunter. Normal- plus invertierte Form ergibt immer ±2.
15. **Zustandslos heißt verdoppelbar heißt bezahlbar:** 320×240 → 640×480 für 18,5 MB/s.
16. **HDMI heißt HSTX.** DVI über PIO wurde geprüft, geht — und ist trotzdem verworfen (4.5).
17. **Auch HSTX braucht die Vorwiderstände.** Der Hardwareblock kodiert, er treibt nicht. 220 Ω trifft mit dem Pad zusammen fast genau die 500 mV.
18. **Bei HSTX ist nur die Pinmenge fix, nicht die Reihenfolge** — die Lane-Zuordnung ist umsortierbar, also löten wie es passt und aufschreiben.
19. **Der DVI Sock ist CC0** — acht Widerstände und eine Buchse, die später einfach auf den eigenen Carrier wandern.
20. **Bleibt der Monitor schwarz: zuerst 5 V ans DVI-Pad**, nicht am Timing drehen.
21. **Stichleitungen sind der Hauptfeind**, nicht die Widerstandswerte. Der Entwurf rechnet mit null Leitungslänge.
