# Hardware

Was hier liegt, zerfällt in zwei Dinge, die nichts miteinander zu tun haben — und bis zum 10.10.2026 lagen sie durcheinander, weshalb das Wertvollere von beiden nicht einmal versioniert war.

| | | |
| - | - | - |
| **Was wir geschrieben haben** | ~300 KB, unersetzlich | **im Git** |
| **Was wir heruntergeladen haben** | ~200 MB, jederzeit wiederbeschaffbar | `vendor/`, **nicht im Git** |

Die Trennlinie ist nicht „wichtig vs. unwichtig", sondern „entsteht hier vs. kommt von woanders". Ein Herstellerfoto ist beim Löten sehr nützlich und gehört trotzdem nicht in ein Repository, das weitergegeben wird: es ist nicht unseres, und es wiegt mehr als alles andere zusammen.

```
Hardware/
    README.md                     diese Datei
    BoardRessourcesManager.md     der Entwurf: warum es den Boardmanager gibt
    profiles/*.gem                ein Brett, eine Datei
    notes/                        beim Nachdenken entstanden
    vendor/                       Fotos, Datenblätter, Schaltpläne, ZIPs  (ignoriert)
```

## Die Pipeline

Ein Brett kommt nicht fertig an. Es wird gekauft, dann bedacht, dann verdrahtet, dann bootet es — und jede dieser Stufen ist eine andere Art von Wahrheit. Die Profilsprache sagt das in einem Wort:

| `STATE` | heißt | |
| ------- | ----- | - |
| `BESTAETIGT` | hat gebootet, ein Bild liegt vor | **darauf fällt man zurück** |
| `VERDRAHTET` | der Draht ist gezogen, die Maschine lief nie | |
| `GEPLANT` | ein Vorhaben. Möglicherweise ein sehr gutes, trotzdem ein Vorhaben | |

Der Zustand steht **in der Datei**, nicht im Ordnernamen. Ein Brett wandert; ein Ordner je Zustand hieße, die Datei bei jeder Erkenntnis zu verschieben, womit jeder Pfad bricht, der auf sie zeigte, und die Geschichte genau dann verlorengeht, wenn sie interessant wird. Außerdem wäre es ein zweiter Ort, an dem der Zustand steht, und zwei Orte widersprechen sich irgendwann.

Die Übersicht macht deshalb ein Werkzeug und kein Verzeichnisbaum:

```
python3 tools/gemboards.py              die Pipeline
python3 tools/gemboards.py --check      sagt nichts, wenn alles stimmt
python3 tools/gemprofile.py <datei>     ein Brett im Einzelnen
```

## Die Bretter

<!-- erzeugt mit: python3 tools/gemboards.py --markdown -->

| Brett | Zustand | Chip | Bildschirm | Ampel | Datei |
| ----- | ------- | ---- | ---------- | ----- | ----- |
| waveshare-pizero-ili9341 | BESTAETIGT | RP2350B | ILI9341 320x240 SPI | rot 12 gelb 6 gruen 30 | `waveshare-pizero.gem` |
| weact-ak | VERDRAHTET | RP2350B | ILI9341 320x240 SPI | rot 14 gelb 6 gruen 28 | `weact-ak.gem` |
| fruitjam | GEPLANT | RP2350B | DVI 400x240 → 800x480 HSTX | rot 29 gelb 8 gruen 11 | `fruitjam.gem` |
| weact-ak-ili9341 | GEPLANT | RP2350B | ILI9341 320x240 SPI | rot 12 gelb 6 gruen 30 | `weact-clone-spi.gem` |

`weact-clone-spi.gem` ist ein überholter Vorentwurf von `weact-ak.gem` — dasselbe Brett, frühere Fassung, noch mit `#` als Kommentarzeichen, das die Sprache inzwischen für die Instanznummer braucht. Es steht noch da, weil Löschen eine Entscheidung ist und keine Aufräumarbeit.

## Reihenfolge beim Beschreiben eines neuen Bretts

1. **`tools/svd2pins.py`** — was kann der Chip? (aus `RP2350.svd`)
2. **`tools/sch2net.py`** — was hat der Brettbauer daraus gemacht? (aus dem Schaltplan)
3. von Hand: das `.gem` schreiben, mit Herkunftsklasse an jeder Zahl
4. **`tools/gemprofile.py`** — was bleibt für einen Sketch übrig, und wo widerspricht sich das Profil?

Schritt 2 ist der, der lange gefehlt hat. Ein gekauftes Brett beschreibt sich dreimal — Pinout-Seite, Boarddefinition des Herstellers, Schaltplan — und nur das letzte ist eine Tatsache. Beim Fruit Jam standen drei Dinge im Schaltplan, die in keiner der anderen Quellen vorkamen, und jedes einzelne änderte einen Plan. Siehe Abschnitt 13.5.1 des Entwurfs.

## `notes/`

Beim Nachdenken entstanden, nicht als Dokumentation gemeint, und deshalb von unterschiedlicher Verlässlichkeit:

| | |
| - | - |
| `videosignal-know-how.md` | wie Videosignale überhaupt funktionieren — die Grundlage für alles mit DVI und RGB |
| `ep5008s-st7701s-bringup.md` | Register und Timings des EP5008S, aus Herstellerunterlagen zusammengetragen |
| `lvds-evaluation.md`, `lvds-evaluation-long.md` | ob LVDS am RP2350B ginge. Die lange Fassung hat Quellen und Pseudocode |
| `carrier-revA-pinout-ENTWURF.md` | **Vorsicht.** Von keiner Platine bestätigt, von ChatGPT erdacht. Steht nur noch da, weil der Gedanke „Panel und HDMI teilen sich GP12–19" daraus hervorging — und der stammt vom Menschen und ist gut |
| `ladder.py`, `tmds_neutral.py` | kleine Rechnereien zu Widerstandsnetzwerken und TMDS |

In `vendor/Display+Carrier/` liegen noch zwei byteidentische Dubletten davon (`… V2.md`, `…(1).md`), wie der Browser sie hinterlassen hat. Sie sind ignoriert und schaden nicht.
