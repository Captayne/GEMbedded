"""Connecty ESP-Werkbank: in Thonny/F5 starten, Bedienung im lokalen Browser."""
import argparse
import base64
import copy
import csv
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
from pathlib import Path
import secrets
import re
import threading
import urllib.parse
import webbrowser

import workbench_core as core


class Workbench:
    def __init__(self, root=core.ROOT):
        self.root = Path(root)
        self.lock = threading.RLock()
        self.cancel = threading.Event()
        self.busy = False
        self.job = ''
        self.logs = []
        self.error = ''
        self.data = {}
        self.new_device()

    def new_device(self):
        self.data = dict(id=core.stamp(), module='Unbekannt / anderes Modul',
                         hardware=None, initial=None, final=None, prepared=None,
                         backup=None, installation=None, files=[], history=[], outcome='Noch nicht evaluiert')
        self.logs = []
        self.error = ''

    def resume(self, path):
        saved = json.loads(Path(path).read_text(encoding='utf-8'))
        if not isinstance(saved.get('id'), str) or not re.fullmatch(r'\d{8}_\d{6}_\d{6}', saved['id']):
            raise ValueError('Ungültige Gerätesitzung.')
        for key in self.data:
            if key in saved:
                self.data[key] = saved[key]
        self.logs = saved.get('logs', [])[-700:]
        self.log('Bedienoberfläche aktualisiert; vorhandene Befunde übernommen. Keine Hardwareaktion ausgeführt.')
        self.document()

    @property
    def folder(self):
        return self.root / 'results' / self.data['id']

    def log(self, line):
        with self.lock:
            self.logs.append(line)
            self.logs = self.logs[-700:]
            self.folder.mkdir(parents=True, exist_ok=True)
            with (self.folder / 'operations.log').open('a', encoding='utf-8') as stream:
                stream.write(line + '\n')

    def snapshot(self):
        with self.lock:
            state = copy.deepcopy(self.data)
            state.update(busy=self.busy, job=self.job, logs=self.logs[:], error=self.error,
                         modules=core.MODULES, catalog_date=core.CATALOG['checked'])
        state['ports'] = core.ports()
        for key in ('initial', 'final'):
            core.enrich_report(state[key], state['hardware'])
        entries = list(core.CATALOG['entries'])
        if state['prepared'] and state['prepared']['plan']['id'].startswith('import-'):
            entries.append(state['prepared']['plan'])
        state['candidates'] = []
        for entry in entries:
            status = core.candidate_status(entry, state['hardware'], state['module'])
            state['candidates'].append(dict(entry, **status,
                                            assessment=core.version_advice(entry, state['final'] or state['initial']),
                                            cached=(self.root / 'firmware' / entry['id'] / 'receipt.json').exists()))
        state['candidates'].sort(key=lambda c: (not c['compatible'], not c['hardware_match']))
        return state

    def start(self, action, params):
        if action not in ('evaluate', 'identify', 'backup', 'prepare', 'install', 'finish',
                          'document', 'new', 'configure', 'import'):
            raise ValueError('Unbekannte Aktion.')
        with self.lock:
            if self.busy:
                raise ValueError('Ein Vorgang läuft bereits.')
            if action == 'new':
                self.new_device()
                return
            if action == 'configure':
                module = params['module']
                if module not in core.MODULES:
                    raise ValueError('Unbekannte Modulvariante.')
                if self.data['installation']:
                    raise ValueError('Nach Installation bleibt das Modulprofil für die Dokumentation fest.')
                self.data['module'] = module
                self.data['prepared'] = None
                self.document()
                return
            self.busy = True
            self.job = action
            self.error = ''
            self.cancel.clear()
        threading.Thread(target=self._worker, args=(action, params), daemon=True).start()

    def _worker(self, action, params):
        try:
            self.perform(action, params)
            self.data['history'].append(dict(time=datetime.now().astimezone().isoformat(), action=action, status='OK'))
        except Exception as exc:
            self.error = str(exc)
            self.log('FEHLER: ' + str(exc))
            self.data['history'].append(dict(time=datetime.now().astimezone().isoformat(), action=action,
                                             status='ERROR', error=str(exc)))
        finally:
            try:
                self.document()
            except Exception as exc:
                self.error += '\nDokumentation fehlgeschlagen: ' + str(exc)
            with self.lock:
                self.busy = False
                self.job = ''

    def require_hardware(self):
        if not self.data['hardware']:
            raise ValueError('Zuerst Chip und Flash im Programmiermodus erkennen.')
        return self.data['hardware']

    def perform(self, action, p):
        port = str(p.get('port', ''))
        if action in ('evaluate', 'identify', 'backup', 'install', 'finish'):
            if port not in [x['device'] for x in core.ports()]:
                raise ValueError('Gewählter Port ist nicht mehr angeschlossen. Ports aktualisieren.')
        before = 'default-reset' if p.get('auto_reset') else 'no-reset'
        if action in ('evaluate', 'finish'):
            if not p.get('run_confirmed'):
                raise ValueError('RUN-Modus und AT-Kommandomodus bestätigen.')
            if action == 'evaluate' and self.data['installation']:
                raise ValueError('Nach Installation bitte Abschlussdiagnose verwenden.')
            baud = int(p['baud']) if p.get('baud') else None
            if baud and not 1200 <= baud <= 5000000:
                raise ValueError('Baudrate außerhalb des Bereichs.')
            report = core.evaluate(port, baud, self.log, self.cancel.is_set)
            key = 'initial' if action == 'evaluate' else 'final'
            self.data[key] = report
            self.data['outcome'] = ('AT antwortet; Funktionsumfang dokumentiert' if report['baud'] else
                                    'Keine AT-Antwort. RUN-Modus, AT-UART und Baudrate prüfen; Firmwarezustand unklar.')
            if action == 'finish' and self.data['installation']:
                self.data['outcome'] = ('Flashen abgeschlossen; AT antwortet nach Neustart. Version und Befehle unten prüfen.'
                                        if report['baud'] else 'Flashdaten geschrieben, aber AT-Abschlussdiagnose fehlgeschlagen.')
        elif action == 'identify':
            if not p.get('prog_confirmed'):
                raise ValueError('Programmiermodus bestätigen.')
            hardware = core.identify(port, self.log, before)
            if self.data['hardware']:
                core.same_device(self.data['hardware'], hardware)
            self.data['hardware'] = hardware
            self.data['outcome'] = 'Chip und Flash erkannt. Modulaufdruck wählen, dann Firmware vergleichen.'
        elif action == 'backup':
            if not p.get('prog_confirmed'):
                raise ValueError('Programmiermodus bestätigen.')
            self.data['backup'] = core.backup(port, self.require_hardware(), self.root / 'backups', self.log, before)
        elif action == 'prepare':
            hardware = self.require_hardware()
            entry = next(x for x in core.CATALOG['entries'] if x['id'] == p.get('candidate'))
            ok, why = core.eligibility(entry, hardware, self.data['module'])
            if not ok:
                raise ValueError(why)
            self.data['prepared'] = None
            self.data['prepared'] = core.prepare(entry, self.root / 'firmware', self.log)
            self.data['outcome'] = 'Firmware heruntergeladen und Paket geprüft. Bereit für die Installationsauswahl.'
        elif action == 'import':
            hardware = self.require_hardware()
            payload = base64.b64decode(p['content'], validate=True)
            if len(payload) > 64 * 1024 * 1024:
                raise ValueError('ZIP zu groß.')
            self.folder.mkdir(parents=True, exist_ok=True)
            archive = self.folder / 'import.zip'
            archive.write_bytes(payload)
            prepared = core.import_package(archive, self.root / 'firmware', self.log)
            ok, why = core.eligibility(prepared['plan'], hardware, self.data['module'])
            if not ok:
                raise ValueError('Paket importiert, aber nicht für dieses Gerät freigegeben: ' + why)
            self.data['prepared'] = prepared
        elif action == 'install':
            if not self.data['prepared'] or not p.get('prog_confirmed') or not p.get('uart_confirmed') or not p.get('overwrite_confirmed'):
                raise ValueError('Firmware auswählen und Programmiermodus, AT-UART sowie Installation bestätigen.')
            self.data['final'] = None
            self.data['installation'] = dict(status='Installation gestartet; Erfolg noch nicht bestätigt',
                                             firmware=self.data['prepared']['plan'])
            self.document()
            result = core.install(port, self.require_hardware(), self.data['module'], self.data['prepared'],
                                  self.root / 'backups', self.log, before,
                                  on_backup=lambda saved: self.data.update(backup=saved))
            self.data['installation'] = result
            self.data['backup'] = result['backup']
            self.data['outcome'] = 'Flashen verifiziert. Jetzt PROG abschalten, resetten und Abschlussdiagnose ausführen.'
        elif action == 'document':
            self.log('Dokumentation gespeichert.')

    def document(self):
        self.folder.mkdir(parents=True, exist_ok=True)
        for key in ('initial', 'final'):
            core.enrich_report(self.data[key], self.data['hardware'])
        # Never combine the old firmware's baud/capabilities with a newly installed build.
        report = (self.data['final'] or {}) if self.data['installation'] else (self.data['final'] or self.data['initial'] or {})
        hw = self.data['hardware'] or {}
        installed = self.data['installation'] or {}
        uart = installed.get('firmware', {}).get('uart', {})
        lines = ['# Connecty – ESP-Modulpass', '', 'Sitzung: ' + self.data['id'],
                 'Modulaufdruck (Anwenderauswahl): ' + self.data['module'],
                 'Ergebnis: ' + self.data['outcome'], '', '## Hardware',
                 f"Chip: {hw.get('chip', 'nicht per ROM verifiziert')}",
                 f"MAC: {hw.get('mac', 'unbekannt')} · Flash: {hw.get('flash_bytes', 'unbekannt')} Bytes",
                 f"ROM-Port: {hw.get('port', 'unbekannt')} · AT-Port: {report.get('port', 'unbekannt')}",
                 '', 'USB VID/PID bestimmt nur den Adapter. Modulvariante und Zuordnung eines separaten AT-Ports werden vom Anwender angegeben.',
                 '', '## Firmware vor dem Update', '```', (self.data['initial'] or {}).get('version', 'Keine Ausgangsdiagnose ermittelt'), '```',
                 '', '## Aktuelle Firmware und Diagnose', '```', report.get('version', 'Keine aktuelle AT-Version ermittelt'), '```',
                 'Diagnose: ' + ('nach Installation / abschließend' if self.data['final'] else
                                'Abschlussdiagnose noch offen' if self.data['installation'] else 'Ausgangszustand'),
                 'Baudrate: ' + str(report.get('baud') or 'unbekannt'),
                 'Befehlsliste: ' + ('laut Firmware vollständig' if report.get('list_complete') else 'Teilbefund'),
                 '', '## Funktionsumfang', '| Funktion | Befund |', '|---|---|']
        lines += [f'| {k} | {v} |' for k, v in report.get('capabilities', {}).items()]
        lines += ['', '## Konkrete Nachweise']
        for feature in report.get('capability_details', []):
            lines += ['', '### ' + feature['name'], feature['summary'], feature['limitation']]
            for probe in feature['probes']:
                lines += ['', probe['command'] + ' → ' + probe['status'], '```text', probe.get('response', '').strip(), '```']
        lines += ['', 'Die Erkennung prüft Befehlsverfügbarkeit, keine WLAN-/Internet-, TLS- oder MQTT-Verbindung.',
                  '', '## Installation und Backup', installed.get('status', 'Keine Installation ausgeführt.')]
        if self.data['backup']:
            lines += ['Backup: ' + self.data['backup']['file'], 'SHA256: ' + self.data['backup']['sha256']]
        if installed.get('firmware'):
            lines += ['Paket: ' + installed['firmware']['title'], 'Quelle: ' + installed['firmware'].get('source', '')]
        lines += ['', '## Integration: Arduino / STM32',
                  f"UART: {report.get('baud') or 'ermitteln'} Baud, 8 Datenbits, keine Parität, 1 Stopbit (8N1).",
                  f"AT-Pins des gewählten installierten Pakets: TX={uart.get('tx', 'nicht verifiziert')}, RX={uart.get('rx', 'nicht verifiziert')}.",
                  'Gemessene UART-Parameter: ' + json.dumps(report.get('uart_config', 'nicht abgefragt/ermittelt'), ensure_ascii=False),
                  f"Flow-Control des installierten Pakets: {uart.get('flow', 'unbekannt')}; ESP-RTS GPIO {uart.get('rts', 'nicht angegeben')}.",
                  'Wenn ESP-RTS aktiv ist: für dauerhaften Datentransfer ESP-RTS mit MCU-CTS verbinden und im Host beachten. Die kurze AT-Inventur nutzt keine PC-Flow-Control.',
                  'Bei unveränderter ESP-01-Firmware: Modul-TX=GPIO1, Modul-RX=GPIO3; bei anderen Modulen Pinbelegung des laufenden Builds prüfen.',
                  'MCU-TX → ESP-RX, MCU-RX ← ESP-TX, gemeinsame Masse, 3,3-V-Logik. 5-V-Arduino benötigt geeignete Pegelanpassung am ESP-RX.',
                  'ESP aus ausreichend belastbarer 3,3-V-Versorgung betreiben. BOOT/EN-Pins gemäß Modul beschalten.',
                  'AT-Zeilen mit CRLF abschließen. Echo, OK/ERROR und asynchrone Meldungen getrennt behandeln.',
                  'Empfang als nichtblockierenden Ringpuffer implementieren; +IPD-Daten längengesteuert lesen (können CRLF und NUL enthalten).',
                  'AT+CIPSEND: erst auf > warten, dann exakt die angekündigte Bytezahl senden und SEND OK/Fehler auswerten.',
                  'WLAN-Zugangsdaten und konkrete Server sind nicht Bestandteil dieser Diagnose.',
                  'TLS-Zertifikatsprüfung, Zeitbasis und passende Cipher Suites gegen den Zielserver separat testen.',
                  '', 'Die Datei module_config.h enthält nur die gemessene Baudrate; unbekannte Pins sind -1.',
                  '', '## Nachgewiesene Befehle', '| Befehl / Form | Beleg |', '|---|---|']
        for cmd in report.get('commands', []):
            forms = ', '.join(k for k in ('test', 'query', 'set', 'execute') if cmd[k])
            lines.append(f"| {cmd['name']} | Firmwareliste: {forms} |")
        for cmd in report.get('probes', []):
            lines.append(f"| {cmd['command']} | {cmd['status']} |")
        lines += ['', 'ERROR bedeutet nicht zwingend „nicht unterstützt“. Rohantworten und Vorher/Nachher-Befunde stehen in session.json.',
                  '', '## Verlauf']
        lines += [f"- {h['time']}: {h['action']} – {h['status']} {h.get('error', '')}" for h in self.data['history']]
        if self.error:
            lines += ['', 'Letzter Fehler: ' + self.error]
        (self.folder / 'modulpass.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
        header = ('/* Generated by Connecty. Unknown values must be configured before use. */\n'
                  '#pragma once\n'
                  f'#define ESP_AT_BAUD {report.get("baud") or 0}UL\n'
                  f'#define ESP_MODULE_TX_GPIO {uart.get("tx", -1)}\n'
                  f'#define ESP_MODULE_RX_GPIO {uart.get("rx", -1)}\n'
                  f'#define ESP_AT_FLOW_CONTROL {report.get("uart_config", {}).get("flow_control", -1)}\n'
                  '#define ESP_AT_LINE_END "\\r\\n"\n')
        (self.folder / 'module_config.h').write_text(header, encoding='utf-8')
        with (self.folder / 'commands.csv').open('w', encoding='utf-8-sig', newline='') as stream:
            writer = csv.writer(stream)
            writer.writerow(['command', 'evidence', 'test', 'query', 'set', 'execute'])
            for c in report.get('commands', []):
                writer.writerow([c['name'], 'firmware-list'] + [c[k] for k in ('test', 'query', 'set', 'execute')])
            for c in report.get('probes', []):
                writer.writerow([c['command'], c['status'], '', '', '', ''])
        names = ['modulpass.md', 'session.json', 'commands.csv', 'module_config.h', 'operations.log']
        self.data['files'] = [dict(name=n, url='/files/' + self.data['id'] + '/' + n) for n in names]
        core.write_json(self.folder / 'session.json', self.data)


def serve(port=8765, open_browser=True, resume=None):
    bench = Workbench()
    if resume:
        bench.resume(resume)
    token = secrets.token_urlsafe(32)
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def send(self, status, body, kind='application/json; charset=utf-8'):
            if not isinstance(body, bytes):
                body = json.dumps(body, ensure_ascii=False).encode('utf-8')
            self.send_response(status)
            self.send_header('Content-Type', kind)
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.send_header('X-Frame-Options', 'DENY')
            self.end_headers()
            self.wfile.write(body)

        def host_ok(self):
            return self.headers.get('Host') == f'127.0.0.1:{self.server.server_port}'

        def do_GET(self):
            if not self.host_ok():
                return self.send(403, {'error': 'Nur localhost erlaubt.'})
            path = urllib.parse.urlparse(self.path).path
            if path == '/':
                html = (core.ROOT / 'workbench.html').read_text(encoding='utf-8').replace('__TOKEN__', token)
                return self.send(200, html.encode('utf-8'), 'text/html; charset=utf-8')
            if path == '/api/state':
                return self.send(200, bench.snapshot())
            if path.startswith('/files/'):
                try:
                    file = core.safe_member(bench.root / 'results', urllib.parse.unquote(path[7:]))
                    if file.name not in ('modulpass.md', 'session.json', 'commands.csv', 'module_config.h', 'operations.log'):
                        raise ValueError('Nicht freigegeben.')
                    return self.send(200, file.read_bytes(), 'text/plain; charset=utf-8')
                except (ValueError, OSError):
                    return self.send(404, {'error': 'Datei nicht vorhanden.'})
            return self.send(404, {'error': 'Nicht gefunden.'})

        def do_POST(self):
            origin = self.headers.get('Origin')
            if (not self.host_ok() or self.headers.get('X-Connecty-Token') != token or
                    origin not in (None, f'http://127.0.0.1:{self.server.server_port}')):
                return self.send(403, {'error': 'Ungültige lokale Anfrage.'})
            try:
                length = int(self.headers.get('Content-Length', '0'))
                if not 0 < length <= 90 * 1024 * 1024:
                    raise ValueError('Anfrage zu groß oder leer.')
                params = json.loads(self.rfile.read(length))
                if self.path == '/api/cancel':
                    if bench.job not in ('evaluate', 'finish'):
                        raise ValueError('Nur AT-Evaluation kann abgebrochen werden. Flashen nicht unterbrechen.')
                    bench.cancel.set()
                elif self.path == '/api/action':
                    bench.start(params.pop('action'), params)
                else:
                    raise ValueError('Unbekannte Adresse.')
                self.send(202, {'ok': True})
            except (ValueError, KeyError, TypeError) as exc:
                self.send(400, {'error': str(exc)})

    server = ThreadingHTTPServer(('127.0.0.1', port), Handler)
    url = f'http://127.0.0.1:{server.server_port}'
    print('Connecty ESP-Werkbank:', url, flush=True)
    print('Zum Beenden Strg+C. Während Flashvorgängen nicht beenden.', flush=True)
    if open_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        if bench.busy:
            print('Vorgang läuft noch. Bitte im Browser abwarten.', flush=True)
            while bench.busy:
                import time
                time.sleep(.5)
    finally:
        server.server_close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--no-browser', action='store_true')
    parser.add_argument('--resume', help='Gespeicherte Gerätesitzung ohne Hardwareaktion wieder öffnen')
    args = parser.parse_args()
    serve(args.port, not args.no_browser, args.resume)
