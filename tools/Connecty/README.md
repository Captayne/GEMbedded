# Connecty ESP-Werkbank

**Die grafische Anwendung ist jetzt `esp_workbench.py`.**
Mit [Start_ESP_Werkbank.cmd](Start_ESP_Werkbank.cmd) starten oder die Python-Datei
in Thonny mit F5 ausführen. Die Bedienung erfolgt im lokalen Browser.

Sie führt durch Evaluation → Chip/Backup → Firmwareauswahl und Download →
Installation → Abschlussdiagnose und Modulpass → nächster ESP.
Details, unterstützte Pakete und eigene ESP32-Builds: [APP_ANLEITUNG.md](APP_ANLEITUNG.md).

## Bisheriges ESP-01-Konsolenwerkzeug

`esp01_check.py` laeuft auf dem Windows-PC, auch in Thonny. Auf dem ESP bleibt
AT-Firmware installiert. Python/MicroPython wird nicht auf den ESP geladen.

## Start in Thonny

1. `esp01_check.py` oeffnen.
2. Unter Interpreter **Lokales Python 3** einstellen. Als alternative Python-Installation
   kann die bereits eingerichtete `.venv\Scripts\python.exe` dieses Projekts dienen.
   Oder im eigenen Thonny-Interpreter ueber **Extras → Pakete verwalten** `pyserial`
   und `esptool` (Version 5.x, Python >= 3.10) installieren.
3. Mit F5 starten, COM-Port eingeben (aktuell wurde COM22 gefunden), Menuepunkt 1.
4. Den kurzen Bericht unter `results/*.md` oeffnen. Daneben steht die JSON-Datei
   mit Rohantworten, USB-Adapterdaten und Baudratensuche.

Alternativ in PowerShell im Projektordner:

```powershell
.\.venv\Scripts\python.exe esp01_check.py
.\.venv\Scripts\python.exe esp01_check.py --port COM22 --check
```

`--ports` listet Ports, `--baud 115200` prueft nur eine Baudrate.
Nicht mehrere Terminalprogramme gleichzeitig auf denselben Port zugreifen lassen.

## Was die Inventur aussagt

- USB-Kennung identifiziert den USB-UART-Adapter, nicht den dahinterliegenden Chip.
- Zwei erfolgreiche `AT`-Antworten bestaetigen die Baudrate (8N1 ohne Flow Control).
- `AT+GMR` liefert die Selbstauskunft der Firmware.
- `AT+CMD?` liefert bei unterstuetzender Firmware deren Befehlsliste samt
  Test-/Abfrage-/Setz-/Ausfuehrungsformen. Die Liste wird dokumentiert, ihre
  mutierenden Befehle werden nicht automatisch ausgefuehrt.
- Bei alter Firmware wird eine Auswahl von Abfragen protokolliert. `ERROR` kann
  auch einen unpassenden Zustand oder eine nicht vorhandene Abfrageform bedeuten.
  Ein vollstaendiger Befehlssatz laesst sich dann nicht sicher automatisch ermitteln.
- Der Check aendert keine Konfiguration, verbindet sich nicht mit einem WLAN,
  loescht nichts und startet das Modul nicht absichtlich neu. Manche USB-Treiber
  koennen beim Portoeffnen dennoch Steuerleitungen kurz umschalten.
- TLS-Handshakes, MQTT-Broker und Datentransfer erfordern eigene Funktionstests
  mit konkretem WLAN/Server. Die Inventur behauptet diese Tests nicht.
- Berichte/Backups koennen Netzwerkdaten oder gespeicherte Zugangsdaten enthalten.

Bei fehlender AT-Antwort nicht automatisch flashen: RUN-Modus, Versorgung, Port,
UART-Pins und abweichende Baudrate pruefen. Eine laufende transparente Verbindung
kann AT-Eingaben als Nutzdaten behandeln; vor dem Check sollte das Modul im
AT-Kommandomodus und nicht in einer aktiven Datenuebertragung sein.

## Aktueller Hardwarebefund

Am 26.09.2026 hat COM22 (CH340, VID:PID 1A86:7523) bei 115200 Baud geantwortet:

```text
AT version:1.3.0.0(Jul 14 2016 18:54:01)
SDK version:2.0.0(5a875ba)
Farylink Technology Co., Ltd. v1.0.0.2
May 11 2017 22:23:58
```

AT-Firmware ist also bereits vorhanden. Die Firmware liefert auf `AT+CMD?`
ERROR. Die Result-Datei nennt die tatsaechlich getesteten Abfragen.
Im ROM-Modus wurde anschliessend verifiziert: **ESP8266EX, 1 MiB Flash**,
26-MHz-Quarz, Flash-Hersteller-ID 0x85, Device-ID 0x6014.
MAC: `b4:e6:2d:1b:89:fc`. Das ROM-Protokoll liegt unter `results/rom_*.txt`.
Damit ist die 1-MiB-Firmwarevariante der relevante Kandidat; ein groesseres
WROOM-02-Flashlayout passt nicht unveraendert.

## Firmwareentscheidung fuer maximale Connectivity

Zielprofil: WLAN Station/SoftAP, TCP/UDP, mehrere Verbindungen, Empfangspufferung,
TLS mit geeigneter Zertifikatspruefung und, soweit im passenden Build vorhanden,
MQTT sowie DNS/SNTP. HTTP und MQTT koennen auch auf dem Host-Mikrocontroller
ueber die Socket-Befehle implementiert werden. Ein einzelner erkannter SSL-Befehl
belegt weder moderne TLS-Kompatibilitaet noch funktionierende Zertifikatspruefung.

ESP8266 hat kein Bluetooth/BLE. ESP32-Binaries und BLE-AT-Kommandos aus den
urspruenglichen Referenzen sind deshalb keine Erweiterungsoption fuer diesen Chip.

Die [offizielle ESP8266-Firmwareseite](https://docs.espressif.com/projects/esp-at/en/release-v2.3.0.0_esp8266/AT_Binary_Lists/ESP8266_AT_binaries.html)
nennt v2.3.0.0 und verweist fuer 1-MB-ESP8266/ESP8285 auf das CI-Artefakt
`esp8285-1MB-at` im Branch `release/v2.3.0.0_esp8266`. Dies ist ein Kandidat,
keine Zusage, dass alle Features in diesem platzbegrenzten Build aktiviert sind.
CI-Artefakte koennen abgelaufen sein; dann ist ein eigener Build erforderlich.

| Flash nach ROM-Erkennung | Vorgehen |
|---|---|
| 512 KiB | Bestehende Firmware sichern; moderne umfangreiche ESP-AT-Builds nicht als passend voraussetzen. Bei Platzmangel Modul mit mehr Flash erwägen. |
| 1 MiB | Passenden 1-MB-Build auf UART-Pins, Flashlayout und aktivierte Protokolle pruefen. |
| 2/4 MiB | ESP8266-Build auf die echte Groesse und ESP-01-Pins abstimmen; mehr Platz allein aktiviert keine Funktionen. |

Am ESP-01 muessen AT-TX/RX auf GPIO1/GPIO3 liegen. CTS/RTS deaktivieren.
Ein WROOM-02-Build kann andere UART-Pins nutzen. Siehe
[Espressifs UART-Konfiguration](https://docs.espressif.com/projects/esp-at/en/release-v2.3.0.0_esp8266/Compile_and_Develop/How_to_set_AT_port_pin.html).
Die Empfehlung im Bericht wird aus Befunden abgeleitet; es wird kein Binary
blind heruntergeladen oder anhand seiner Versionsnummer automatisch geflasht.

## ROM-Erkennung, Backup und Flashen

Menuepunkt 2 liest Chip-/Flashinformationen mit Espressifs `esptool` aus.
Dafuer muss GPIO0 **waehrend Reset/Einschalten** auf GND liegen. Je nach
OPEN-SMART-Variante gibt es einen PROG-Schalter, eine Taste oder es ist eine
Verbindung von GPIO0 nach GND noetig. Anschliessend RUN/GPIO0 freigeben und resetten.
Der ESP benoetigt 3,3-V-Versorgung/Logik; USB-5-V nicht direkt an ESP-Pins anlegen.

Menuepunkt 3 sichert den gesamten erkannten Flash und schreibt eine SHA256-Datei.
Menuepunkt 4 erwartet ein bewusst erstelltes JSON-Manifest des passenden Builds.
Beispielschema (Platzhalter sind absichtlich nicht flashbar):

```json
{
  "chip": "esp8266",
  "flash_bytes": 1048576,
  "uart_tx": 1,
  "uart_rx": 3,
  "source": "URL und Build/Commit des tatsaechlich passenden Pakets",
  "segments": [
    {"offset": "0x0", "file": "PASSENDES_FACTORY_IMAGE.bin", "sha256": "SHA256_DES_IMAGES"}
  ]
}
```

Adressen und Segmente **exakt aus dem Flashlayout des Firmwarepakets** uebernehmen.
0x0 im Schema gilt nur fuer ein dafuer gebautes Gesamtimage. Einzelne Bootloader,
Anwendung und Parameterdateien benoetigen ihre jeweils vorgesehenen Adressen.
Die UART-Felder sind eine zu pruefende Deklaration, keine automatische Binary-Analyse.
Das Programm prueft Hashes, Bereichsgrenzen und Sektorueberlappungen, legt vor dem
Schreiben ein frisches Gesamtbackup an und vergleicht dessen Groesse mit dem
Manifest. `esptool` schreibt und verifiziert; ein zweiter Verify-Aufruf folgt.
Ein komplettes Erase wird nicht automatisch ausgefuehrt. Falls ein Firmwarewechsel
ein vollstaendiges Loeschen voraussetzt, muss dessen Migrationsablauf gesondert
vorbereitet werden. Flashen wurde bislang nicht an dieser Hardware getestet.

Nach dem Flashen: RUN-Modus, Reset, erneute Inventur und erst danach Zielservertests.

Referenzen: [AT+CMD und Basisbefehle](https://docs.espressif.com/projects/esp-at/en/release-v2.3.0.0_esp8266/AT_Command_Set/Basic_AT_Commands.html),
[esptool fuer ESP8266](https://docs.espressif.com/projects/esptool/en/latest/esp8266/).
