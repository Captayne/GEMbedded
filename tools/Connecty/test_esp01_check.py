import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from esp01_check import command_list, exchange, recommend, validate_manifest


class FakePort:
    def __init__(self, chunks):
        self.chunks = list(chunks)
        self.written = b''

    @property
    def in_waiting(self):
        return 0

    def read(self, count):
        return self.chunks.pop(0) if self.chunks else b''

    def reset_input_buffer(self):
        pass

    def write(self, data):
        self.written += data

    def flush(self):
        pass


class Checks(unittest.TestCase):
    def test_fragmented_response_and_echo(self):
        port = FakePort([b'AT+GMR\r\n+INFO:OK inside text\r\n', b'\r\nO', b'K\r\n'])
        result = exchange(port, 'AT+GMR', 0.1)
        self.assertEqual(result['status'], 'OK')
        self.assertEqual(port.written, b'AT+GMR\r\n')

    def test_error_and_timeout(self):
        self.assertEqual(exchange(FakePort([b'\r\nERROR\r\n']), 'AT', .01)['status'], 'ERROR')
        self.assertEqual(exchange(FakePort([b'OK without newline']), 'AT', .01)['status'], 'TIMEOUT')

    def test_command_flags(self):
        rows = command_list('+CMD:1,"+CIPSTART",0,0,1,0\r\n+CMD:2,"AT",0,0,0,1\r\nOK\r\n')
        self.assertEqual(rows[0]['name'], 'AT+CIPSTART')
        self.assertTrue(rows[0]['set'])
        self.assertFalse(rows[0]['query'])
        self.assertEqual(rows[1]['name'], 'AT')

    def test_no_response_is_not_empty_flash(self):
        self.assertIn('beweist weder einen leeren Flash', recommend({'baud': None}))

    def test_flash_validation(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            data = b'firmware' * 600
            (root / 'image.bin').write_bytes(data)
            segment = dict(offset='0x0', file='image.bin', sha256=hashlib.sha256(data).hexdigest())
            spec = dict(chip='esp8266', flash_bytes=1048576, uart_tx=1, uart_rx=3, segments=[segment])
            path = root / 'manifest.json'
            path.write_text(json.dumps(spec))
            self.assertEqual(len(validate_manifest(path)[1]), 1)
            spec['segments'].append(dict(segment, offset='0x1000'))
            path.write_text(json.dumps(spec))
            with self.assertRaisesRegex(ValueError, 'ueberlappen'):
                validate_manifest(path)
            spec['segments'] = [dict(segment, sha256='0' * 64)]
            path.write_text(json.dumps(spec))
            with self.assertRaisesRegex(ValueError, 'SHA256'):
                validate_manifest(path)
            spec['uart_tx'] = 15
            path.write_text(json.dumps(spec))
            with self.assertRaisesRegex(ValueError, 'UART'):
                validate_manifest(path)


if __name__ == '__main__':
    unittest.main()
