"""ESP-01 Inventur, fuer Desktop-Python/Thonny (kein MicroPython)."""
import argparse
import csv
from datetime import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

import serial
from serial.tools import list_ports

ROOT = Path(__file__).resolve().parent
DOC = 'https://docs.espressif.com/projects/esp-at/en/release-v2.3.0.0_esp8266/'
BAUDS = (115200, 9600, 57600, 38400, 19200, 74880, 230400, 460800)
# Ausschliesslich Abfragen; kein Join, Reset, Speichern oder Verbindungsaufbau.
QUERIES = ('AT+GMR', 'AT+CMD?', 'AT+UART_CUR?', 'AT+UART?',
           'AT+CWMODE?', 'AT+CWMODE_CUR?', 'AT+CWAUTOCONN?',
           'AT+CIPSTA?', 'AT+CIPAP?', 'AT+CWDHCP?', 'AT+CIPSTAMAC?',
           'AT+CIPSTATUS', 'AT+CIPMUX?',
           'AT+CIPMODE?', 'AT+CIPDINFO?', 'AT+CIPRECVMODE?',
           'AT+CIPSSLCCONF?', 'AT+CIPSSLSIZE?', 'AT+CIPDNS?',
           'AT+CIPSNTPCFG?', 'AT+MQTTCONN?', 'AT+SYSRAM?', 'AT+SLEEP?',
           'AT+RFPOWER?', 'AT+CIPSERVERMAXCONN?', 'AT+CIPSTO?')


def stamp():
    return datetime.now().strftime('%Y%m%d_%H%M%S_%f')


def command_list(raw):
    result = []
    for line in raw.splitlines():
        if not line.startswith('+CMD:'):
            continue
        try:
            row = next(csv.reader([line[5:]]))
            if len(row) != 6 or any(x not in ('0', '1') for x in row[2:]):
                continue
            name = row[1].strip()
            if not name.startswith('AT'):
                name = 'AT' + (name if name.startswith('+') else '+' + name)
            result.append(dict(name=name, test=row[2] == '1', query=row[3] == '1',
                               set=row[4] == '1', execute=row[5] == '1'))
        except (ValueError, csv.Error):
            continue
    return result


def exchange(port, command, timeout=3):
    port.reset_input_buffer()
    port.write((command + '\r\n').encode('ascii'))
    port.flush()
    data = bytearray()
    deadline = time.monotonic() + timeout
    status = 'TIMEOUT'
    while time.monotonic() < deadline:
        data.extend(port.read(port.in_waiting or 1))
        # Nur komplette Abschlusszeilen werten, niemals Echo oder Teilstrings.
        lines = bytes(data).split(b'\n')[:-1]
        terminal = [x.strip() for x in lines if x.strip() in (b'OK', b'ERROR', b'FAIL')]
        if terminal:
            status = terminal[-1].decode()
            break
        if len(data) > 256000:
            status = 'LIMIT'
            break
    return dict(command=command, status=status,
                response=data.decode('utf-8', errors='replace'), raw_hex=data.hex())


def open_serial(device, baud):
    port = serial.Serial(port=None, baudrate=baud, timeout=0.1, write_timeout=2)
    port.dtr = False
    port.rts = False
    port.port = device
    port.open()
    return port


def recommend(report):
    if not report.get('baud'):
        return ('Keine AT-Antwort nachgewiesen. Das beweist weder einen leeren Flash noch '
                'defekte Firmware. RUN-Modus, Versorgung, UART-Pins, Baudrate und ggf. '
                'Transparentmodus pruefen; danach optional ROM-Chip-/Flash-Check.')
    names = {c['name'] for c in report['commands']}
    wanted = {'AT+CIPSTART', 'AT+CIPSEND', 'AT+CIPRECVDATA',
              'AT+CIPSSLCCONF', 'AT+MQTTUSERCFG', 'AT+MQTTCONN', 'AT+MQTTPUB'}
    missing = sorted(wanted - names)
    if report['list_complete'] and not missing:
        return ('AT-Firmware mit den gewuenschten Socket-, TLS-Konfigurations- und MQTT-'
                'Befehlen vorhanden: vorerst behalten. Kein Update allein aufgrund der '
                'Versionsnummer. TLS/Datentransfer mit dem Zielserver separat testen.')
    detail = ('Nicht in der Firmware-Liste: ' + ', '.join(missing) + '. '
              if report['list_complete'] else 'Vollstaendige Befehlsliste nicht abrufbar. ')
    return ('AT-Firmware vorhanden. ' + detail + 'Update ist ein Kandidat, noch keine '
            'Notwendigkeit: zuerst Flashgroesse im ROM-Modus bestimmen. Fuer 1 MiB '
            'ESP8266 den esp8285-1MB-at Build aus release/v2.3.0.0_esp8266 pruefen '
            '(CI-Artefakt oder Eigenbau); UART TX=GPIO1, RX=GPIO3 und kein Hardware-'
            'Flow-Control erforderlich. WROOM-02-Binary nicht ungeprueft verwenden. '
            'Bei 512 KiB ist der Funktionsumfang stark begrenzt; aktueller Build muss '
            'nachweislich passen. MQTT/HTTP koennen alternativ im Host ueber TCP laufen. '
            'Eine universelle Maximal-Firmware ohne Kenntnis von Flash und Build gibt es nicht.')


def save_report(report):
    folder = ROOT / 'results'
    folder.mkdir(exist_ok=True)
    base = folder / ('esp_' + stamp())
    report['recommendation'] = recommend(report)
    base.with_suffix('.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    lines = ['# ESP AT-Pruefung', '', 'Zeit: ' + report['time'],
             'Port/USB-Adapter: ' + json.dumps(report['port'], ensure_ascii=False),
             'AT-Baudrate: ' + str(report.get('baud') or 'nicht erkannt'), '',
             '## Bewertung', report['recommendation'], '',
             'USB VID/PID identifiziert den Adapter, nicht den ESP-Chip. '
             'Chip/Flash sind ohne separaten ROM-Check nicht verifiziert.', '',
             '## Firmware', '```', report.get('version', 'unbekannt').strip(), '```', '',
             '## Befehlsinventar',
             ('Firmware meldet vollstaendige Liste.' if report['list_complete'] else
              'Teilbefund: keine vollstaendige Firmware-Liste. ERROR bedeutet nicht zwingend ununterstuetzt.'),
             '', '| Befehl | Test =? | Abfrage ? | Setzen = | Ausfuehren |',
             '|---|---|---|---|---|']
    for c in report['commands']:
        lines.append('| ' + c['name'] + ' | ' + ' | '.join('ja' if c[k] else '-' for k in
                     ('test', 'query', 'set', 'execute')) + ' |')
    if not report['commands']:
        lines += ['', 'Nachgewiesene Befehlsformen (jeweils OK): ' + ', '.join(
            '`' + r['command'] + '`' for r in report['probes'] if r['status'] == 'OK')]
    if report.get('error'):
        lines += ['', 'Portfehler: ' + report['error']]
    lines += ['', '## Tatsaechliche Abfragen', '| Abfrage | Ergebnis |', '|---|---|']
    lines += [f"| {r['command']} | {r['status']} |" for r in report['probes']]
    lines += ['', 'OK bestaetigt nur die jeweilige Abfrage, keinen funktionierenden '
              'Internet-/TLS-/MQTT-Datentransfer. TIMEOUT ist ungeklaert. '
              'Rohantworten und Baudratensuche stehen in der JSON-Datei.', '',
              'Firmware-Kandidaten: ' + DOC + 'AT_Binary_Lists/ESP8266_AT_binaries.html']
    base.with_suffix('.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print('\nErgebnis:', base.with_suffix('.md'))
    print(report['recommendation'])
    return report


def inspect(device, baud=None):
    info = next((p for p in list_ports.comports() if p.device == device), None)
    report = dict(time=datetime.now().astimezone().isoformat(),
                  port=dict(device=device, description=getattr(info, 'description', None),
                            hwid=getattr(info, 'hwid', None)),
                  baud=None, probes=[], discovery=[], commands=[], list_complete=False)
    try:
        with open_serial(device, baud or BAUDS[0]) as port:
            time.sleep(1)
            for speed in ([baud] if baud else BAUDS):
                print('Pruefe', device, speed, flush=True)
                port.baudrate = speed
                time.sleep(0.15)
                answers = [exchange(port, 'AT', 0.8) for _ in range(2)]
                report['discovery'].append(dict(baud=speed, answers=answers))
                if all(a['status'] == 'OK' for a in answers):
                    report['baud'] = speed
                    break
            if report['baud']:
                for cmd in QUERIES:
                    answer = exchange(port, cmd, 10 if cmd == 'AT+CMD?' else 2)
                    report['probes'].append(answer)
                    print(cmd, answer['status'], flush=True)
                    if cmd == 'AT+GMR':
                        report['version'] = answer['response']
                    if cmd == 'AT+CMD?':
                        report['commands'] = command_list(answer['response'])
                        report['list_complete'] = answer['status'] == 'OK' and bool(report['commands'])
    except (serial.SerialException, OSError) as exc:
        report['error'] = str(exc)
        print('Portfehler:', exc)
    return save_report(report)


def esptool(device, args):
    cmd = [sys.executable, '-m', 'esptool', '--chip', 'esp8266', '--port', device,
           '--baud', '115200', '--before', 'no-reset', '--after', 'no-reset'] + args
    result = subprocess.run(cmd, capture_output=True, text=True, errors='replace', timeout=300)
    output = result.stdout + result.stderr
    print(output)
    folder = ROOT / 'results'
    folder.mkdir(exist_ok=True)
    (folder / ('rom_' + stamp() + '.txt')).write_text(output, encoding='utf-8')
    if result.returncode:
        raise RuntimeError('esptool fehlgeschlagen. BOOT-Modus und Port pruefen.')
    return output


def boot_prompt():
    print('ROM-Modus: GPIO0 beim Reset auf GND (ggf. PROG-Schalter), dann Reset/neu einstecken.')
    print('Nach Abschluss GPIO0 freigeben/RUN waehlen und neu starten.')
    input('Wenn das Modul im ROM-Modus bereit ist: Enter (Strg+C bricht ab) ')


def backup(device):
    folder = ROOT / 'backups'
    folder.mkdir(exist_ok=True)
    path = folder / ('esp8266_' + stamp() + '.bin')
    esptool(device, ['read-flash', '0', 'ALL', str(path)])
    if not path.exists() or not path.stat().st_size:
        raise RuntimeError('Kein Backup erzeugt.')
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    path.with_suffix('.sha256').write_text(digest + '  ' + path.name + '\n', encoding='ascii')
    return path


def validate_manifest(path):
    spec = json.loads(path.read_text(encoding='utf-8'))
    if spec['chip'] != 'esp8266' or spec['uart_tx'] != 1 or spec['uart_rx'] != 3:
        raise ValueError('Manifest passt nicht zu ESP-01 / UART GPIO1 und GPIO3.')
    size = spec['flash_bytes']
    if size not in (524288, 1048576, 2097152, 4194304):
        raise ValueError('Ungueltige Flashgroesse.')
    segments = []
    for item in spec['segments']:
        offset = int(str(item['offset']), 0)
        file = (path.parent / item['file']).resolve()
        data = file.read_bytes()
        if not data or offset < 0 or offset % 4096 or offset + len(data) > size:
            raise ValueError('Segment leer, unaligned oder ausserhalb des Flashs.')
        if hashlib.sha256(data).hexdigest().lower() != item['sha256'].lower():
            raise ValueError('SHA256 stimmt nicht: ' + str(file))
        segments.append((offset, file, len(data)))
    segments.sort()
    if not segments:
        raise ValueError('Keine Segmente.')
    for prev, nxt in zip(segments, segments[1:]):
        if ((prev[0] + prev[2] + 4095) // 4096) * 4096 > nxt[0]:
            raise ValueError('Flash-Sektoren ueberlappen.')
    return spec, segments


def flash(device, manifest):
    spec, segments = validate_manifest(manifest)
    print(json.dumps(spec, indent=2, ensure_ascii=False))
    print('Adressen, Flashgroesse, UART und Features muessen aus dem passenden Firmware-Build stammen.')
    if input('Zum Sichern und anschliessenden Ueberschreiben FLASH eingeben: ') != 'FLASH':
        return
    boot_prompt()
    saved = backup(device)
    if saved.stat().st_size != spec['flash_bytes']:
        raise ValueError('Erkannte Flashgroesse passt nicht zum Manifest; kein Schreiben.')
    args = ['write-flash']
    for offset, file, _ in segments:
        args += [hex(offset), str(file)]
    esptool(device, args)
    esptool(device, ['verify-flash'] + args[1:])
    print('Flash verifiziert. Backup:', saved)
    print('Jetzt RUN-Modus, Reset und erneut AT-Pruefung starten.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port')
    parser.add_argument('--baud', type=int)
    parser.add_argument('--check', action='store_true', help='nur AT-Inventur ohne Menue')
    parser.add_argument('--ports', action='store_true')
    args = parser.parse_args()
    ports = list(list_ports.comports())
    for p in ports:
        print(p.device, p.description, p.hwid)
    if args.ports:
        return
    device = args.port or input('COM-Port des ESP-Adapters (z.B. COM3): ').strip()
    if not device:
        return
    if args.check:
        inspect(device, args.baud)
        return
    while True:
        print('\n1 AT pruefen + Result-Datei\n2 ROM Chip/Flash identifizieren\n3 Flash sichern\n4 Firmware-Manifest flashen\n0 Ende')
        action = input('Auswahl: ').strip()
        try:
            if action == '0':
                break
            if action == '1':
                inspect(device, args.baud)
            elif action == '2':
                boot_prompt()
                esptool(device, ['flash-id'])
            elif action == '3':
                boot_prompt()
                print('Backup:', backup(device))
            elif action == '4':
                flash(device, Path(input('Manifest-Datei: ').strip().strip('"')).resolve())
        except (OSError, ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired) as exc:
            print('Abgebrochen:', exc)


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print('\nAbgebrochen.')
