"""Hardware and firmware services for the local Connecty application."""
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
import zipfile

from serial.tools import list_ports
from esp01_check import BAUDS, QUERIES, command_list, exchange, open_serial, stamp

ROOT = Path(__file__).resolve().parent
CATALOG = json.loads((ROOT / 'firmware_catalog.json').read_text(encoding='utf-8'))
MODULES = ['Unbekannt / anderes Modul', 'ESP-01 / ESP-01S', 'ESP-WROOM-02', 'ESP32-WROOM-32',
           'ESP32-WROVER', 'ESP32-S2', 'ESP32-S3', 'ESP32-C2', 'ESP32-C3', 'ESP32-C6']
CHIPS = ('esp8266', 'esp32', 'esp32s2', 'esp32s3', 'esp32c2', 'esp32c3', 'esp32c5',
         'esp32c6', 'esp32c61', 'esp32h2')


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + '.tmp')
    temp.write_text(json.dumps(value, indent=2, ensure_ascii=False), encoding='utf-8')
    temp.replace(path)


def ports():
    return [dict(device=p.device, description=p.description, hwid=p.hwid) for p in list_ports.comports()]


def evaluate(port, baud=None, log=lambda s: None, cancelled=lambda: False):
    report = dict(time=datetime.now().astimezone().isoformat(), port=port, baud=None,
                  commands=[], probes=[], discovery=[], list_complete=False)
    with open_serial(port, baud or 115200) as stream:
        time.sleep(0.5)
        for rate in ([baud] if baud else BAUDS):
            if cancelled():
                raise RuntimeError('Evaluation abgebrochen.')
            log(f'Prüfe {port} mit {rate} Baud …')
            stream.baudrate = rate
            time.sleep(.15)
            replies = [exchange(stream, 'AT', .8) for _ in range(2)]
            report['discovery'].append(dict(baud=rate, replies=replies))
            if all(r['status'] == 'OK' for r in replies):
                report['baud'] = rate
                break
        if report['baud']:
            for cmd in QUERIES:
                if cancelled():
                    raise RuntimeError('Evaluation abgebrochen.')
                reply = exchange(stream, cmd, 12 if cmd == 'AT+CMD?' else 2)
                report['probes'].append(reply)
                log(cmd + ' → ' + reply['status'])
                if cmd == 'AT+GMR':
                    report['version'] = reply['response'].strip()
                if cmd == 'AT+CMD?':
                    report['commands'] = command_list(reply['response'])
                    listed = sum(line.startswith('+CMD:') for line in reply['response'].splitlines())
                    report['list_complete'] = bool(listed) and listed == len(report['commands']) and reply['status'] == 'OK'
                if cmd in ('AT+UART_CUR?', 'AT+UART?') and reply['status'] == 'OK':
                    match = re.search(r'\+UART(?:_CUR)?:\s*(\d+),(\d+),(\d+),(\d+),(\d+)', reply['response'])
                    if match:
                        report['uart_config'] = dict(zip(('baud', 'data_bits', 'stop_bits', 'parity', 'flow_control'), map(int, match.groups())))
    report['capabilities'] = capabilities(report)
    return report


def capability_details(report, hardware=None):
    names = {c['name'] for c in report.get('commands', [])}
    groups = {
        'WLAN': (['AT+CWMODE', 'AT+CWJAP'], ['AT+CWMODE?', 'AT+CWMODE_CUR?', 'AT+CWAUTOCONN?',
                 'AT+CIPSTA?', 'AT+CIPAP?', 'AT+CWDHCP?', 'AT+CIPSTAMAC?']),
        'TCP / UDP': (['AT+CIPSTART', 'AT+CIPSEND'], ['AT+CIPSTATUS', 'AT+CIPMUX?', 'AT+CIPMODE?',
                      'AT+CIPDINFO?', 'AT+CIPSTO?', 'AT+CIPSERVERMAXCONN?']),
        'TLS / SSL': (['AT+CIPSSLCCONF'], ['AT+CIPSSLCCONF?', 'AT+CIPSSLSIZE?']),
        'MQTT': (['AT+MQTTUSERCFG', 'AT+MQTTCONN', 'AT+MQTTPUB'], ['AT+MQTTCONN?']),
        'SNTP / NTP': (['AT+CIPSNTPCFG', 'AT+CIPSNTPTIME'], ['AT+CIPSNTPCFG?', 'AT+CIPSNTPTIME?']),
        'HTTP': (['AT+HTTPCLIENT'], []),
        'BLE': (['AT+BLEINIT'], []),
        'Gepufferter Empfang': (['AT+CIPRECVMODE', 'AT+CIPRECVDATA'], ['AT+CIPRECVMODE?']),
    }
    limits = {
        'WLAN': 'WLAN-Beitritt, Scan und Datenübertragung wurden nicht ausgeführt.',
        'TCP / UDP': 'Es wurde keine TCP-/UDP-Verbindung geöffnet und kein Nutzdatenpaket übertragen.',
        'TLS / SSL': 'Eine SSL-Konfigurationsabfrage belegt keinen TLS-Handshake, keine TLS-Version und keine Zertifikatsprüfung.',
        'MQTT': 'Broker-Verbindung, Anmeldung und Publish/Subscribe wurden nicht getestet.',
        'SNTP / NTP': 'Die Inventur startet keine Zeitsynchronisation. Erreichbarkeit eines NTP-Servers und Richtigkeit der Zeit wurden nicht geprüft.',
        'HTTP': 'Es wurde keine HTTP-Anfrage ausgeführt.',
        'BLE': 'BLE-Scan, Verbindung und Datenaustausch wurden nicht getestet.',
        'Gepufferter Empfang': 'Es wurden keine gepufferten Nutzdaten empfangen oder ausgelesen.',
    }
    result = []
    for title, (commands, probes) in groups.items():
        evidence = [p for p in report.get('probes', []) if p['command'] in probes]
        good = [p['command'] for p in evidence if p['status'] == 'OK']
        listed = [c for c in report.get('commands', []) if c['name'] in commands]
        if all(c in names for c in commands):
            summary = 'In der Firmwareliste: ' + ', '.join(commands) + '.'
        elif good:
            summary = 'Erfolgreich beantwortet: ' + ', '.join(good) + '.'
        elif report.get('list_complete'):
            summary = 'Nicht in der Firmwareliste: ' + ', '.join(c for c in commands if c not in names) + '.'
        else:
            summary = 'Nicht nachgewiesen (kein Beweis für fehlende Unterstützung).'
        if title == 'BLE' and hardware and hardware.get('chip') == 'esp8266':
            summary = 'Auf diesem Chip nicht verfügbar: ESP8266 besitzt keine Bluetooth-/BLE-Hardware.'
        result.append(dict(name=title, summary=summary, limitation=limits[title], probes=evidence, listed=listed))
    return result


def capabilities(report):
    return {item['name']: item['summary'] for item in capability_details(report)}


def enrich_report(report, hardware=None):
    if report is not None:
        report['capability_details'] = capability_details(report, hardware)
        report['capabilities'] = {c['name']: c['summary'] for c in report['capability_details']}
    return report


def candidate_status(entry, hardware, module):
    compatible, reason = eligibility(entry, hardware, module)
    matches = bool(hardware and entry['chip'] == hardware['chip'] and entry['flash_bytes'] == hardware['flash_bytes'])
    needs_module = matches and module == MODULES[0]
    if needs_module:
        reason = 'Chip und Flashgröße passen. Bauform bestätigen: ' + ' / '.join(entry['modules']) + '.'
    return dict(compatible=compatible, reason=reason, hardware_match=matches, needs_module=needs_module)


def run_tool(port, args, log, chip='auto', before='no-reset', timeout=900):
    if chip not in CHIPS + ('auto',) or before not in ('no-reset', 'default-reset'):
        raise ValueError('Unzulässiges Chip-/Resetprofil.')
    python = Path(sys.executable)
    if python.name.lower() == 'pythonw.exe':
        python = python.with_name('python.exe')
    cmd = [str(python), '-u', '-m', 'esptool', '--chip', chip, '--port', port,
           '--baud', '115200', '--before', before, '--after', 'no-reset'] + list(args)
    env = dict(os.environ, PYTHONIOENCODING='utf-8', NO_COLOR='1', TERM='dumb')
    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, encoding='utf-8', errors='replace', env=env,
                               creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    expired = threading.Event()
    def stop():
        expired.set()
        process.kill()
    timer = threading.Timer(timeout, stop)
    timer.start()
    lines = []
    try:
        for line in process.stdout:
            lines.append(line)
            log(line.rstrip())
        code = process.wait()
    finally:
        timer.cancel()
        process.stdout.close()
    output = ''.join(lines)
    if expired.is_set() or code:
        raise RuntimeError('esptool fehlgeschlagen. Details im Protokoll. ' +
                           ('Zeitlimit überschritten.' if expired.is_set() else output[-900:]))
    return output


def parse_rom(text):
    chip_match = re.search(r'Connected to (ESP[\w-]+)', text)
    if not chip_match:
        chip_match = re.search(r'Chip(?: type| is):?\s+(ESP[\w-]+)', text)
    if not chip_match:
        raise ValueError('Chiptyp in esptool-Ausgabe nicht erkannt.')
    raw = chip_match[1].lower().replace('-', '')
    chip = raw if raw in CHIPS else None
    if raw == 'esp8266ex':
        chip = 'esp8266'
    elif raw.startswith(('esp32d0wd', 'esp32d2wd', 'esp32picod4', 'esp32u4wdh')):
        chip = 'esp32'
    size = re.search(r'Detected flash size:\s*(\d+)\s*(KB|MB)', text, re.I)
    mac = re.search(r'MAC:\s*([0-9a-f:]{17})', text, re.I)
    if not chip or not size or not mac:
        raise ValueError('Chip, Flashgröße oder MAC nicht eindeutig erkannt; kein Flashen freigegeben.')
    return dict(chip=chip, flash_bytes=int(size[1]) * (1024 if size[2].upper() == 'KB' else 1048576),
                mac=mac[1].lower(), raw=text)


def identify(port, log, before='no-reset'):
    info = parse_rom(run_tool(port, ['flash-id'], log, before=before))
    # esptool itself also enforces secure-device restrictions; never pass --force.
    info['port'] = port
    return info


def same_device(expected, actual):
    if any(expected[k] != actual[k] for k in ('chip', 'flash_bytes', 'mac')):
        raise ValueError('Anderes Gerät erkannt. Bitte „Nächster ESP“ wählen und neu evaluieren.')


def backup(port, hardware, folder, log, before='no-reset'):
    live = identify(port, log, before)
    same_device(hardware, live)
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / (hardware['mac'].replace(':', '') + '_' + stamp() + '.bin')
    run_tool(port, ['read-flash', '0', str(hardware['flash_bytes']), str(path)], log, chip=hardware['chip'])
    if not path.exists() or path.stat().st_size != hardware['flash_bytes']:
        raise ValueError('Backupgröße stimmt nicht. Kein Schreiben erlaubt.')
    result = dict(file=str(path), sha256=digest(path), bytes=path.stat().st_size,
                  chip=hardware['chip'], mac=hardware['mac'])
    write_json(path.with_suffix('.json'), result)
    log('Vollbackup gesichert: ' + path.name)
    return result


def eligibility(entry, hardware, module):
    if not hardware:
        return False, 'Zuerst Chip und Flash im Programmiermodus erkennen.'
    if entry['chip'] != hardware['chip']:
        return False, 'Für einen anderen Chiptyp.'
    if hardware['flash_bytes'] != entry['flash_bytes']:
        return False, f"Layout benötigt exakt {entry['flash_bytes'] // 1024} KiB; erkannt: {hardware['flash_bytes'] // 1024} KiB."
    if module not in entry['modules']:
        return False, 'Modulvariante passt nicht oder wurde noch nicht ausgewählt.'
    if entry.get('recipe') == 'external':
        return False, entry['note']
    return True, 'Chip, Flashlayout und gewählte Modulvariante passen. UART-Verdrahtung vor Installation prüfen.'


def version_advice(entry, report):
    if not report or not report.get('baud'):
        return 'Keine laufende AT-Version nachgewiesen. Installation erst nach Prüfung von RUN-Modus und AT-Verdrahtung entscheiden.'
    match = re.search(r'(?:Bin version:|AT version:)\s*v?(\d+(?:\.\d+)+)', report.get('version', ''), re.I)
    if not match or not re.fullmatch(r'\d+(?:\.\d+)+', entry.get('version', '')):
        return 'Versionsvergleich nicht eindeutig. Funktionsnachweise und Build-Herkunft vergleichen.'
    def version(text):
        result = [int(x) for x in text.split('.')]
        return tuple((result + [0] * 4)[:4])
    old, new = version(match[1]), version(entry['version'])
    if old == new:
        return 'Gleiche gemeldete Version: normalerweise behalten. Ein anderer Build kann trotzdem andere Funktionen besitzen.'
    if old > new:
        return 'Gemeldete Version ist neuer: dieses Paket wäre möglicherweise ein Downgrade. Kein Updatevorteil allein aus der Versionsnummer.'
    return 'Neuere Versionsnummer als gemeldet. Nutzen anhand der Funktionen prüfen; Versionsreihen und Hersteller-Builds sind nicht immer direkt vergleichbar.'


def safe_member(root, name):
    name = name.replace('\\', '/')
    parts = PurePosixPath(name)
    if parts.is_absolute() or '..' in parts.parts or ':' in name or not name:
        raise ValueError('Unsicherer Pfad im Firmwarepaket.')
    target = (Path(root) / name).resolve()
    if not target.is_relative_to(Path(root).resolve()):
        raise ValueError('Pfad verlässt Firmwareordner.')
    return target


def unpack(archive, dest):
    with zipfile.ZipFile(archive) as z:
        if len(z.infolist()) > 5000 or sum(i.file_size for i in z.infolist()) > 256 * 1024 * 1024:
            raise ValueError('Firmwarearchiv zu groß.')
        seen = set()
        for item in z.infolist():
            target = safe_member(dest, item.filename)
            if (item.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError('Symlinks im Paket nicht erlaubt.')
            if item.is_dir():
                continue
            key = str(target).casefold()
            if key in seen:
                raise ValueError('Doppelte Dateipfade im Archiv.')
            seen.add(key)
            target.parent.mkdir(parents=True, exist_ok=True)
            with z.open(item) as src, target.open('wb') as out:
                shutil.copyfileobj(src, out)


def size_bytes(value):
    match = re.fullmatch(r'(\d+)(KB|MB)', value)
    if not match:
        raise ValueError('Unbekannte Flashgröße im Paket.')
    return int(match[1]) * (1024 if match[2] == 'KB' else 1048576)


def validate_plan(plan, directory):
    if plan['chip'] not in CHIPS or not isinstance(plan['flash_bytes'], int):
        raise ValueError('Chip/Flash ungültig.')
    if plan['flash_bytes'] not in [2 ** n for n in range(18, 27)]:
        raise ValueError('Flashgröße ungültig.')
    uart = plan['uart']
    if not all(isinstance(uart.get(k), int) and 0 <= uart[k] <= 48 for k in ('tx', 'rx')):
        raise ValueError('AT-UART-Pins fehlen oder sind ungültig.')
    if uart['tx'] == uart['rx'] or uart.get('flow') not in ('none', 'rts') or not 1200 <= uart.get('baud', 0) <= 5000000:
        raise ValueError('Nur UART ohne Flow-Control oder mit ESP-RTS unterstützt; CTS-gesteuerte Builds benötigen einen erweiterten Adapterablauf.')
    if uart['flow'] == 'rts' and not isinstance(uart.get('rts'), int):
        raise ValueError('RTS-Pin fehlt.')
    if 'ESP-01 / ESP-01S' in plan['modules'] and (plan['chip'] != 'esp8266' or uart['tx'] != 1 or uart['rx'] != 3):
        raise ValueError('ESP-01 benötigt ESP8266 und AT-UART TX=1, RX=3.')
    opts = plan.get('options', {})
    for key, value in opts.items():
        allowed = {'flash_mode': ['dio', 'dout', 'qio', 'qout', 'keep'],
                   'flash_freq': ['20m', '26m', '40m', '80m', 'keep'],
                   'flash_size': ['256KB', '512KB', '1MB', '2MB', '4MB', '8MB', '16MB', '32MB', '64MB']}
        if key not in allowed or value not in allowed[key]:
            raise ValueError('Unzulässige Flashoption.')
    if opts.get('flash_size') and size_bytes(opts['flash_size']) != plan['flash_bytes']:
        raise ValueError('Widersprüchliche Flashgröße.')
    regions = []
    for item in plan['segments']:
        offset = int(str(item['offset']), 0)
        path = safe_member(directory, item['file'])
        length = path.stat().st_size
        if not length or offset < 0 or offset % 4096 or offset + length > plan['flash_bytes']:
            raise ValueError('Flashsegment außerhalb des Layouts oder nicht sektorausgerichtet.')
        if digest(path) != item['sha256']:
            raise ValueError('Firmwaredatei verändert: ' + path.name)
        regions.append((offset, ((offset + length + 4095) // 4096) * 4096))
    regions.sort()
    if not regions or any(a[1] > b[0] for a, b in zip(regions, regions[1:])):
        raise ValueError('Leeres Layout oder überlappende Flashsektoren.')
    return plan


def package_plan(entry, directory):
    directory = Path(directory)
    plan = dict(entry)
    if entry['recipe'] == 'nano':
        roots = list(directory.glob('**/bin/at/512+512/user1.1024.new.2.bin'))
        if len(roots) != 1:
            raise ValueError('Erwartete Nano-Firmware fehlt.')
        base = roots[0].parents[2]
        pairs = [(0, base / 'boot_v1.7.bin'), (0x1000, roots[0]),
                 (0xFC000, base / 'esp_init_data_default_v08.bin'),
                 (0x7E000, base / 'blank.bin'), (0xFE000, base / 'blank.bin')]
        plan['options'] = dict(flash_mode='dout', flash_freq='40m', flash_size='1MB')
        # Full-size image ensures stale configuration is cleared, after full backup.
    else:
        files = list(directory.glob('**/flasher_args.json'))
        if len(files) != 1:
            raise ValueError('Eindeutiges flasher_args.json fehlt. Paket mit connecty.json importieren.')
        data = json.loads(files[0].read_text(encoding='utf-8'))
        if data.get('extra_esptool_args', {}).get('chip', entry['chip']) != entry['chip']:
            raise ValueError('Chip im Paket widerspricht dem Katalog.')
        settings = data['flash_settings']
        if size_bytes(settings['flash_size']) != entry['flash_bytes']:
            raise ValueError('Flashgröße im Paket widerspricht dem Katalog.')
        plan['options'] = settings
        pairs = [(int(addr, 0), safe_member(files[0].parent, file)) for addr, file in data['flash_files'].items()]
        config = files[0].parent / 'sdkconfig'
        if config.exists():
            config_text = config.read_text(encoding='utf-8', errors='replace')
            plan['build_options'] = [line for line in config_text.splitlines()
                                     if re.match(r'CONFIG_AT_\w*(?:SUPPORT|COMMAND_SUPPORT)=y$', line)]
            flow = re.search(r'^CONFIG_AT_UART_DEFAULT_FLOW_CONTROL=(\d+)$', config_text, re.M)
            if flow and int(flow[1]) != {'none': 0, 'rts': 1}[plan['uart']['flow']]:
                raise ValueError('UART-Flow-Control im Paket weicht vom Katalog ab.')
        # Modern official packages carry UART config in manufacturing NVS CSV.
        csv_path = files[0].parent / 'customized_partitions/mfg_nvs.csv'
        if csv_path.exists():
            import csv
            fields = {r[0]: r[-1] for r in csv.reader(csv_path.read_text(encoding='utf-8').splitlines()) if len(r) >= 4}
            for name, key in [('uart_tx_pin', 'tx'), ('uart_rx_pin', 'rx'), ('uart_baudrate', 'baud')]:
                if name in fields:
                    if int(fields[name]) != plan['uart'][key]:
                        raise ValueError('UART-Konfiguration im Paket weicht vom Katalog ab; Katalog prüfen.')
    plan['segments'] = [dict(offset=hex(offset), file=str(path.relative_to(directory)).replace('\\', '/'), sha256=digest(path))
                        for offset, path in pairs]
    return validate_plan(plan, directory)


def prepare(entry, firmware_dir, log):
    if not entry.get('url'):
        raise ValueError('Kein direkter Download verfügbar. Hinweise im Vorschlag beachten.')
    firmware_dir = Path(firmware_dir)
    firmware_dir.mkdir(parents=True, exist_ok=True)
    destination = firmware_dir / entry['id']
    receipt_path = destination / 'receipt.json'
    if receipt_path.exists():
        receipt = json.loads(receipt_path.read_text(encoding='utf-8'))
        plan = validate_plan(receipt['plan'], destination)
        if digest(destination / 'package.zip') != receipt['archive_sha256']:
            raise ValueError('ZIP-Prüfsumme im Cache stimmt nicht. Paketordner prüfen.')
        # Re-evaluate catalog metadata (e.g. UART details) without downloading again.
        plan = package_plan(entry, destination / 'contents')
        for segment in plan['segments']:
            segment['file'] = 'contents/' + segment['file']
        receipt['plan'] = plan
        write_json(receipt_path, receipt)
        log('Geprüftes Paket aus dem Firmwareordner wiederverwendet.')
        return dict(directory=str(destination), plan=plan, receipt=receipt)
    # Temporary directory belongs to this operation; no partial cache is accepted.
    with tempfile.TemporaryDirectory(prefix='.download-', dir=firmware_dir) as temporary:
        work = Path(temporary)
        archive = work / 'package.zip'
        req = urllib.request.Request(entry['url'], headers={'User-Agent': 'Connecty-ESP-Workbench/1.0'})
        log('Lade ' + entry['url'])
        with urllib.request.urlopen(req, timeout=45) as response, archive.open('wb') as out:
            if not response.url.startswith('https://'):
                raise ValueError('Download-Weiterleitung ohne HTTPS abgelehnt.')
            total = 0
            while chunk := response.read(262144):
                total += len(chunk)
                if total > 64 * 1024 * 1024:
                    raise ValueError('Download zu groß.')
                out.write(chunk)
        log(f'{total // 1024} KiB geladen. Prüfe Paket …')
        unpack(archive, work / 'contents')
        plan = package_plan(entry, work / 'contents')
        for segment in plan['segments']:
            segment['file'] = 'contents/' + segment['file']
        receipt = dict(plan=plan, archive_sha256=digest(archive), source=entry['url'],
                       downloaded=datetime.now().astimezone().isoformat(),
                       integrity='SHA256 lokal berechnet; keine separate Hersteller-Signatur behauptet')
        write_json(work / 'receipt.json', receipt)
        if destination.exists():
            raise ValueError('Unvollständiger Cache vorhanden; Ordner umbenennen und erneut laden.')
        shutil.copytree(work, destination)
    return dict(directory=str(destination), plan=plan, receipt=receipt)


def import_package(archive, firmware_dir, log):
    firmware_dir = Path(firmware_dir)
    firmware_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.import-', dir=firmware_dir) as temporary:
        work = Path(temporary)
        unpack(archive, work)
        manifests = list(work.glob('**/connecty.json'))
        if len(manifests) != 1:
            raise ValueError('Import benötigt genau eine connecty.json mit Chip, Modul, UART und Flashlayout. Siehe Anleitung.')
        plan = json.loads(manifests[0].read_text(encoding='utf-8'))
        plan['recipe'] = 'import'
        validate_plan(plan, manifests[0].parent)
        ident = 'import-' + digest(archive)[:16]
        plan['id'] = ident
        destination = firmware_dir / ident
        plan.setdefault('note', 'Lokales Paket; Angaben zu Modul und UART stammen vom Ersteller.')
        plan.setdefault('features', 'Build-spezifisch; durch Abschlussdiagnose prüfen.')
        plan.setdefault('source', 'Lokaler Import')
        plan.setdefault('version', 'benutzerdefiniert')
        plan.setdefault('title', ident)
        receipt = dict(plan=plan, archive_sha256=digest(archive), source='Lokaler Import')
        if not destination.exists():
            shutil.copytree(manifests[0].parent, destination)
            shutil.copy2(archive, destination / 'package.zip')
            write_json(destination / 'receipt.json', receipt)
    log('Lokales Paket importiert.')
    return dict(directory=str(destination), plan=validate_plan(plan, destination), receipt=receipt)


def install(port, hardware, module, prepared, folder, log, before='no-reset', on_backup=lambda result: None):
    directory = Path(prepared['directory'])
    plan = validate_plan(prepared['plan'], directory)
    allowed, reason = eligibility(plan, hardware, module)
    if not allowed:
        raise ValueError(reason)
    # Always create a fresh backup from the same physical device immediately before write.
    saved = backup(port, hardware, folder, log, before)
    on_backup(saved)
    if digest(saved['file']) != saved['sha256']:
        raise ValueError('Backup-Prüfsumme stimmt nicht.')
    # esptool merge-bin patches the bootloader header at its chip-specific offset.
    # Full-size output clears stale configuration; no separate erase operation.
    target = Path(folder) / ('install_' + stamp() + '.bin')
    merge = ['merge-bin', '--output', str(target), '--fill-flash-size',
             str(plan['flash_bytes'] // 1048576) + 'MB' if plan['flash_bytes'] >= 1048576 else '512KB']
    for key, value in plan.get('options', {}).items():
        merge += ['--' + key.replace('_', '-'), value]
    for part in plan['segments']:
        path = safe_member(directory, part['file'])
        if digest(path) != part['sha256']:
            raise ValueError('Firmwaredatei während Vorbereitung verändert.')
        merge += [str(part['offset']), str(path)]
    run_tool(port, merge, log, chip=hardware['chip'])
    if target.stat().st_size != plan['flash_bytes']:
        raise ValueError('Zusammengeführtes Flashimage hat falsche Größe.')
    args = ['write-flash', '0x0', str(target)]
    log('Backup fertig. Installiere vollständiges Flashlayout; gespeicherte Einstellungen werden ersetzt.')
    run_tool(port, args, log, chip=hardware['chip'])
    # write-flash verifies written bytes and reports failure; options may patch image header.
    log('Schreiben und esptool-Datenprüfung erfolgreich. RUN-Modus einschalten, dann Abschlussdiagnose.')
    return dict(backup=saved, firmware=plan, image=str(target), image_sha256=digest(target),
                status='Flash geschrieben und von esptool geprüft; AT-Abschlussdiagnose noch offen')
