import copy
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import workbench_core as core
from esp_workbench import Workbench

ROM = '''Connected to ESP8266 on COM22:
Chip type: ESP8266EX
MAC: b4:e6:2d:1b:89:fc
Detected flash size: 1MB
'''
MODULE = 'ESP-01 / ESP-01S'


class CoreTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def fixture(self):
        file = self.root / 'test.bin'
        file.write_bytes(b'\xe9' + b'\0' * 4095)
        plan = dict(id='test', title='test build', chip='esp8266', flash_bytes=1048576,
                    modules=[MODULE], uart=dict(tx=1, rx=3, baud=115200, flow='none'),
                    segments=[dict(offset='0x0', file='test.bin', sha256=core.digest(file))],
                    options=dict(flash_size='1MB', flash_mode='dout', flash_freq='40m'))
        return dict(directory=str(self.root), plan=plan)

    def test_rom_variants_and_missing_identity(self):
        self.assertEqual(core.parse_rom(ROM)['flash_bytes'], 1048576)
        for source, target in [('ESP32-C3', 'esp32c3'), ('ESP32-S3', 'esp32s3'), ('ESP32-D0WD', 'esp32')]:
            self.assertEqual(core.parse_rom(ROM.replace('ESP8266', source))['chip'], target)
        with self.assertRaises(ValueError):
            core.parse_rom('Connected to ESP32 on COM1:')
        with self.assertRaises(ValueError):
            core.parse_rom(ROM.replace('ESP8266', 'ESP32-P4'))

    def test_compatibility_rejects_wrong_flash_module_chip(self):
        entry = core.CATALOG['entries'][0]
        hardware = core.parse_rom(ROM)
        self.assertTrue(core.eligibility(entry, hardware, MODULE)[0])
        for change in [dict(chip='esp32'), dict(flash_bytes=2097152)]:
            self.assertFalse(core.eligibility(entry, dict(hardware, **change), MODULE)[0])
        self.assertFalse(core.eligibility(entry, hardware, 'ESP-WROOM-02')[0])
        self.assertFalse(core.eligibility(core.CATALOG['entries'][1], hardware, MODULE)[0])

    def test_zip_traversal_and_duplicate_entries(self):
        for name in ('../evil.bin', 'C:/evil.bin', '/evil.bin', '..\\evil.bin'):
            archive = self.root / 'bad.zip'
            with zipfile.ZipFile(archive, 'w') as z:
                z.writestr(name, b'x')
            with self.assertRaises(ValueError):
                core.unpack(archive, self.root / 'out')

    def test_manifest_checks_hash_bounds_pins_and_overlap(self):
        prepared = self.fixture()
        plan = prepared['plan']
        core.validate_plan(plan, self.root)
        for mutate in [lambda p: p['segments'][0].update(sha256='0' * 64),
                       lambda p: p['segments'][0].update(offset='0x100000'),
                       lambda p: p['segments'].append(dict(p['segments'][0])),
                       lambda p: p['uart'].update(tx=15),
                       lambda p: p['options'].update(force='yes')]:
            invalid = copy.deepcopy(plan)
            mutate(invalid)
            with self.assertRaises(ValueError):
                core.validate_plan(invalid, self.root)

    def test_no_write_after_failed_backup_or_changed_device(self):
        prepared = self.fixture()
        with patch.object(core, 'backup', side_effect=ValueError('Backup fehlgeschlagen')), patch.object(core, 'run_tool') as run:
            with self.assertRaises(ValueError):
                core.install('COM22', core.parse_rom(ROM), MODULE, prepared, self.root, lambda x: None)
            run.assert_not_called()
        with patch.object(core, 'identify', return_value=dict(core.parse_rom(ROM), mac='00:00:00:00:00:00')), patch.object(core, 'run_tool') as run:
            with self.assertRaises(ValueError):
                core.backup('COM22', core.parse_rom(ROM), self.root, lambda x: None)
            run.assert_not_called()

    def test_backup_before_merge_and_write(self):
        prepared = self.fixture()
        calls = []
        saved = self.root / 'backup.bin'
        saved.write_bytes(b'\xff' * 1048576)
        def fake_backup(*args):
            calls.append('backup')
            return dict(file=str(saved), sha256=core.digest(saved))
        def fake_tool(port, args, *pos, **kwargs):
            calls.append(args[0])
            if args[0] == 'merge-bin':
                Path(args[args.index('--output') + 1]).write_bytes(b'\xff' * 1048576)
        with patch.object(core, 'backup', side_effect=fake_backup), patch.object(core, 'run_tool', side_effect=fake_tool):
            result = core.install('COM22', core.parse_rom(ROM), MODULE, prepared, self.root, lambda x: None)
        self.assertEqual(calls, ['backup', 'merge-bin', 'write-flash'])
        self.assertIn('offen', result['status'])

    def test_error_is_not_missing_feature(self):
        report = dict(commands=[], probes=[dict(command='AT+MQTTCONN?', status='ERROR')], list_complete=False)
        self.assertIn('kein Beweis', core.capabilities(report)['MQTT'])

    def test_workflow_documents_before_after_and_resets(self):
        app = Workbench(self.root)
        start = dict(port='COM22', baud=115200, version='AT version:1.3.0.0', commands=[], probes=[], list_complete=False)
        final = dict(start, version='AT version:1.7.5.0')
        with patch.object(core, 'ports', return_value=[dict(device='COM22')]), patch.object(core, 'evaluate', side_effect=[start, final]):
            app.perform('evaluate', dict(port='COM22', run_confirmed=True))
            app.perform('finish', dict(port='COM22', run_confirmed=True))
        app.document()
        data = json.loads((app.folder / 'session.json').read_text(encoding='utf-8'))
        self.assertNotEqual(data['initial']['version'], data['final']['version'])
        self.assertTrue((app.folder / 'module_config.h').exists())
        old_folder = app.folder
        app.start('new', {})
        self.assertIsNone(app.data['hardware'])
        self.assertIsNone(app.data['initial'])
        self.assertIsNone(app.data['prepared'])
        self.assertTrue((old_folder / 'modulpass.md').exists())

    def test_install_requires_explicit_ui_choices(self):
        app = Workbench(self.root)
        app.data['hardware'] = core.parse_rom(ROM)
        app.data['prepared'] = self.fixture()
        with patch.object(core, 'ports', return_value=[dict(device='COM22')]), patch.object(core, 'install') as install:
            with self.assertRaises(ValueError):
                app.perform('install', dict(port='COM22', prog_confirmed=True))
            install.assert_not_called()

    def test_busy_prevents_new_device(self):
        app = Workbench(self.root)
        app.busy = True
        with self.assertRaises(ValueError):
            app.start('new', {})

    def test_pending_install_does_not_export_old_baud_as_current(self):
        app = Workbench(self.root)
        app.data['initial'] = dict(baud=9600, version='old', port='COM22')
        app.data['installation'] = dict(status='pending', firmware=self.fixture()['plan'])
        app.document()
        self.assertIn('ESP_AT_BAUD 0UL', (app.folder / 'module_config.h').read_text())

    def test_version_advice_same_and_downgrade(self):
        entry = core.CATALOG['entries'][0]
        self.assertIn('Gleiche', core.version_advice(entry, dict(baud=115200, version='AT version:1.7.5.0')))
        self.assertIn('Downgrade', core.version_advice(entry, dict(baud=115200, version='AT version:2.3.0.0')))

    def test_missing_module_is_actionable_not_missing_firmware(self):
        entry = core.CATALOG['entries'][0]
        pending = core.candidate_status(entry, core.parse_rom(ROM), core.MODULES[0])
        self.assertTrue(pending['hardware_match'])
        self.assertTrue(pending['needs_module'])
        self.assertFalse(pending['compatible'])
        self.assertTrue(core.candidate_status(entry, core.parse_rom(ROM), MODULE)['compatible'])
        wrong = core.candidate_status(core.CATALOG['entries'][3], core.parse_rom(ROM), core.MODULES[0])
        self.assertFalse(wrong['needs_module'])

    def test_evidence_keeps_actual_response_and_failed_probe(self):
        report = dict(commands=[], probes=[
            dict(command='AT+CIPSSLSIZE?', status='OK', response='+CIPSSLSIZE:2048\r\nOK\r\n'),
            dict(command='AT+CIPSSLCCONF?', status='ERROR', response='ERROR\r\n')], list_complete=False)
        details = core.capability_details(report, core.parse_rom(ROM))
        ssl = next(d for d in details if d['name'] == 'TLS / SSL')
        self.assertIn('AT+CIPSSLSIZE?', ssl['summary'])
        self.assertEqual(len(ssl['probes']), 2)
        self.assertIn('2048', ssl['probes'][0]['response'])
        self.assertIn('keinen TLS-Handshake', ssl['limitation'])
        self.assertIn('keine Bluetooth', next(d for d in details if d['name'] == 'BLE')['summary'])
        report['probes'].append(dict(command='AT+CIPSNTPCFG?', status='ERROR', response='ERROR\r\n'))
        ntp = next(d for d in core.capability_details(report) if d['name'] == 'SNTP / NTP')
        self.assertEqual(ntp['probes'][0]['status'], 'ERROR')
        self.assertIn('kein Beweis', ntp['summary'])

    def test_resume_preserves_diagnosis_and_exports_evidence(self):
        app = Workbench(self.root)
        app.data['initial'] = dict(baud=115200, commands=[], probes=[dict(command='AT+CIPSSLSIZE?',
                                   status='OK', response='+CIPSSLSIZE:2048\r\nOK')], list_complete=False)
        app.document()
        resumed = Workbench(self.root)
        resumed.resume(app.folder / 'session.json')
        self.assertEqual(resumed.data['id'], app.data['id'])
        self.assertIn('+CIPSSLSIZE:2048', (resumed.folder / 'modulpass.md').read_text(encoding='utf-8'))


if __name__ == '__main__':
    unittest.main()
