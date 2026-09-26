# Connecty ESP-Werkbank

## Starten

**Doppelklick auf `Start_ESP_Werkbank.cmd`.** Die bereits eingerichtete lokale
Python-Umgebung wird verwendet; der Standardbrowser öffnet die App.

In Thonny: `esp_workbench.py` öffnen, **lokales Python** auswählen, F5.
Benötigt Python 3.11 oder neuer und die Pakete aus `requirements.txt`.
Thonny verwendet ggf. eine eigene Python-Installation; dort die Pakete ebenfalls
installieren oder `.venv\Scripts\python.exe` als Interpreter wählen.

Die Bedienadresse ist `http://127.0.0.1:8765`. Python bleibt im Hintergrund aktiv.
Die App bindet ausschließlich an localhost. Bei belegtem Port:

```powershell
.\.venv\Scripts\python.exe esp_workbench.py --port 8766
```

## Der Ablauf

1. **Evaluation:** AT-Port wählen, RUN-/Kommandomodus bestätigen, starten.
   Version, erfolgreiche Abfragen und Funktionshinweise erscheinen im Textfeld.
   Bei unterstützender Firmware wird `AT+CMD?` vollständig ausgewertet. Sonst
   bleiben Befehle ausdrücklich ein Teilbefund. Bestehende AT-Firmware darf
   behalten werden. WLAN-Zugangsdaten sind für diese Inventur nicht erforderlich.
2. **Chip & Backup:** PROG/BOOT einschalten, resetten, Programmierport und
   tatsächlichen Modulaufdruck auswählen. Die ROM-Abfrage erkennt Chip, MAC
   und Flashgröße. Ein Vollbackup ist hier separat möglich.
3. **Firmware wählen:** Die App erklärt, welche Pakete passen und weshalb andere
   nicht freigegeben sind. Auswahl herunterladen; vorhandene Pakete werden anhand
   gespeicherter SHA256-Werte geprüft und wiederverwendet. Ohne passendes Paket
   wird kein beliebiges Ersatzimage angeboten.
   Fehlt lediglich der Modulaufdruck, steht am passenden Chip-/Flashlayout
   „Bauform noch bestätigen“. Bauform direkt dort oder im Auswahlfeld ergänzen.
   Eine unbekannte Bauform bedeutet nicht, dass keine Firmware verfügbar ist.
4. **Installieren:** Angezeigte AT-Pins und Modus prüfen. Nach bewusster Auswahl
   sichert die App den gesamten Flash frisch, prüft die Geräteidentität und schreibt
   ein vollständiges Image. Esptool prüft die geschriebenen Daten. Dabei werden
   bisherige Einstellungen ersetzt. Nicht abstecken oder das Python-Fenster schließen.
5. **Diagnose & Doku:** PROG aus, resetten, AT-Port auswählen (bei ESP32 häufig ein
   anderer UART). Die erneute Diagnose wird mit dem Ausgangszustand gespeichert.
   Der Modulpass kann im Arduino-/STM32-Projekt weiterverwendet werden.
6. **Nächster ESP:** setzt sämtliche Gerätebefunde, Freigaben und Firmwareauswahl
   zurück. Frühere Dokumente, Backups und Firmwarepakete bleiben erhalten.

Bei einer Portnummeränderung nach Umstecken „Ports aktualisieren“ wählen.
Die automatische DTR/RTS-Umschaltung nur bei dafür beschalteten Boards aktivieren.
Beim ESP-01 GPIO0 beim Reset auf GND legen; beim normalen Start wieder freigeben.
Für ESP32-Varianten gelten die BOOT/EN-Pins der jeweiligen Platine; GPIO0 ist
nicht auf allen Varianten der BOOT-Pin.

Die geprüften WROOM-02- und WROOM-32-Pakete aktivieren ESP-RTS als Flow-Control
(GPIO1 bzw. GPIO14). Für längere Host-Transfers an MCU-CTS anschließen und beachten.
Die AT-Inventur sendet kurze Befehle ohne PC-Flow-Control; eine erfolgreiche
Inventur ist kein Belastungstest. Die tatsächlich abgefragten UART-Parameter
stehen im Modulpass. Imports unterstützen `flow: "none"` oder `flow: "rts"`
mit zusätzlichem `rts`-Pin; CTS-/RTS+CTS-Builds werden nicht freigegeben.

## Was derzeit unterstützt wird

ROM-Erkennung und Backup verwenden esptool mit automatischer Chip-Erkennung.
Unterstützte Familien im Programm: ESP8266, ESP32, S2, S3, C2, C3, C5, C6,
C61 und H2. AT-Inventur ist chipunabhängig. Nicht jeder Chip besitzt WLAN/BLE
oder eine von Espressif bereitgestellte AT-Firmware. Unbekannte Ergebnisse werden
nicht als Geräteunterstützung ausgegeben. Hardwaretests erfolgten bisher nur am ESP8266EX.

Der mitgelieferte Katalog enthält bewusst konkrete, geprüfte Pakete:

| Paket | Bedingungen | Einordnung |
|---|---|---|
| ESP8266 NonOS AT 1.7.5 Nano | ESP-01/01S, genau 1 MiB, TX1/RX3 | Direktdownload; Legacy, eingeschränkte SSL-Cipher-Suites, kein Versprechen für MQTT/HTTP. |
| ESP-AT 2.3 für 1 MiB | ESP8266/ESP8285, angepasste UART-Pins | Hinweis auf offiziellen CI-/Eigenbauweg; kein separat veröffentlichtes Direktdownload-Release. Import möglich. |
| ESP-WROOM-02 AT 2.3.0.0 | ESP-WROOM-02, genau 2 MiB, TX15/RX13 | Direktdownload; Build-Optionen werden aus sdkconfig gelesen. |
| ESP32-WROOM-32 AT 4.1.1.0 | WROOM-32, genau 4 MiB, TX17/RX16 | Direktdownload; 4.2.0.0 über Herstellerantrag verfügbar. |

Andere ESP32-Module verwenden einen passenden eigenen Import. **Die generische
Erkennung ist kein automatischer Firmwarekatalog für jede Modulvariante.**
Modulaufdruck lässt sich nicht zuverlässig aus der USB- oder Chipkennung ableiten.
Ein größeres Flash ist nicht pauschal als kompatibel mit einem kleineren Layout
freigegeben. Ungeprüfte Mischungen von Partitionen oder UART-Pins werden vermieden.

Offizielle Quellen sind in jeder Firmwarekarte verlinkt. Katalogstand: 26.09.2026.
Downloads nutzen HTTPS. Die lokale SHA256-Prüfung erkennt geänderte Cachedateien;
sie wird nicht als Hersteller-Signatur ausgegeben. Es werden keine Programme oder
Skripte aus Firmwarearchiven ausgeführt.

## Ergebnisse und Weiterverwendung

- `results/<Sitzung>/modulpass.md`: lesbarer Befund mit Integrationshinweisen.
- `session.json`: Vorher/Nachher, Rohantworten, Hardware, Paket, Backup und Verlauf.
- `commands.csv`: Befehlsinventar mit Art des Nachweises.
- `module_config.h`: gemessene Baudrate und bekannte AT-Pins; unbekannte Werte sind 0/-1.
- `operations.log`: fortlaufendes Protokoll, auch bei Fehlern.
- `backups/*.bin` plus JSON: vollständiger Originalflash mit MAC/Chip/Hash.
- `firmware/<Paket>/`: wiederverwendbarer Download, entpackte Daten und Prüfbeleg.

Ein Modulpass wird auch bei Fehlern gespeichert. Ein fehlgeschlagener AT-Test nach
Flashen wird als solcher angezeigt; Schreibprüfung und funktionierende AT-Verbindung
sind getrennte Ergebnisse. Aktuell kann die App keine Serverfunktionstests ersetzen:
TLS-Zertifikate, Uhrzeit, Brokerzugang und Datentransfer müssen mit dem späteren
Zielsystem geprüft werden. Berichte und insbesondere Flashbackups können gespeicherte
Netzwerkdaten enthalten. Dateien entsprechend behandeln.

In der Diagnose stehen zu jeder Funktion die tatsächlich gesendeten Abfragen,
deren Status und die vollständigen Antworten. Gelistete Befehle und noch nicht
ausgeführte Funktionstests werden gesondert erläutert. Die Tabelle „Alle Abfragen“
enthält auch Systemabfragen, die keiner Connectivity-Funktion zugeordnet sind.
Diese Nachweise erscheinen ebenfalls im Modulpass.

## Eigene Pakete für weitere ESP32-Module

ZIP mit genau einer `connecty.json`; alle Dateien sind relativ dazu. Beispiel:

```json
{
  "title": "Mein ESP32-C3 AT-Build",
  "version": "Build-Version/Commit",
  "chip": "esp32c3",
  "modules": ["ESP32-C3"],
  "flash_bytes": 4194304,
  "uart": {"tx": 21, "rx": 20, "baud": 115200, "flow": "none"},
  "source": "URL oder Build-Herkunft",
  "features": "Hier nur tatsächlich aktivierte Funktionen angeben",
  "options": {"flash_mode": "dio", "flash_freq": "40m", "flash_size": "4MB"},
  "segments": [
    {"offset": "0x0", "file": "factory.bin", "sha256": "ECHTE_SHA256_DER_DATEI"}
  ]
}
```

**Das Beispiel ist keine Firmwareempfehlung und ohne Anpassung nicht flashbar.**
Pins, Chip, Adressen, Imageart und Größe müssen aus genau diesem Build stammen.
Ein Factory-Gesamtimage an 0x0 ist nur zulässig, wenn es für diesen Offset gebaut
wurde. Alternativ alle einzelnen Dateien mit ihren vorgesehenen Offsets angeben.
SHA256 z.B. mit `Get-FileHash -Algorithm SHA256` berechnen und kleingeschrieben
eintragen. Die App prüft Hashes, Grenzen, Sektorüberlappung, Chip-/Modulzuordnung.
Manifestangaben zum UART sind eine Deklaration des Erstellers, keine automatische
Rekonstruktion der Firmware. Secure-Boot-/verschlüsselte Geräte sind kein Ziel
dieses Workflows; esptool-Schutzmechanismen werden nicht übersteuert.

## Prüfungen

```powershell
.\.venv\Scripts\python.exe -m unittest -v
.\.venv\Scripts\python.exe verify_packages.py
```

Der zweite Befehl lädt Katalogpakete bzw. nutzt den Cache und prüft die vollständige
Flashimage-Erzeugung. Er verbindet sich nicht mit Hardware und schreibt keinen ESP.
Das bisherige `esp01_check.py` bleibt als separates Konsolenwerkzeug erhalten.
