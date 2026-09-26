"""Development check of official package layouts; never connects to hardware."""
import io
import urllib.request
import zipfile
import sys
sys.stdout.reconfigure(encoding='utf-8')

URLS = [
    'https://dl.espressif.com/esp-at/firmwares/esp32/ESP32-WROOM-32/ESP32-WROOM-32-AT-V4.1.1.0.zip',
    'https://dl.espressif.com/esp-at/firmwares/esp8266/ESP-WROOM-02-AT-V2.3.0.0.zip',
    'https://www.espressif.com/sites/default/files/ap/ESP8266_NonOS_AT_Bin_V1.7.5_1.zip',
]
if __name__ == '__main__':
    for url in URLS:
        print('\nURL', url)
        try:
            with urllib.request.urlopen(url, timeout=45) as response:
                data = response.read(64 * 1024 * 1024)
            archive = zipfile.ZipFile(io.BytesIO(data))
            print('\n'.join(n for n in archive.namelist() if not n.startswith('__MACOSX'))[:5000])
            for name in archive.namelist():
                if not name.startswith('__MACOSX') and name.endswith(('download.config', 'flash_args', 'flasher_args.json', 'readme.txt', 'README.md')):
                    print(name, archive.read(name).decode(errors='replace')[:6000])
        except Exception as exc:
            print(type(exc).__name__, str(exc))
