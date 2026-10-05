"""Erzeugt die Chip-Funktionsmatrix aus dem SVD des pico-sdk.  Nicht von Hand pflegen."""
import csv, re, sys, xml.etree.ElementTree as ET

SVD = sys.argv[1] if len(sys.argv) > 1 else r"C:\Installed\pico-sdk\src\rp2350\hardware_regs\RP2350.svd"
OUT = sys.argv[2] if len(sys.argv) > 2 else "rp2350_pins.csv"
ADC_BASE, ADC_N = 40, 8          # RP2350B; der A hat 26/4 (platform_defs.h)

io = next(p for p in ET.parse(SVD).getroot().iter('peripheral')
          if p.findtext('name') == 'IO_BANK0')
rows, slots = {}, set()
for reg in io.iter('register'):
    m = re.fullmatch(r'GPIO(\d+)_CTRL', reg.findtext('name') or '')
    if not m:
        continue
    f = next(f for f in reg.iter('field') if f.findtext('name') == 'FUNCSEL')
    d = {int(ev.findtext('value'), 0): ev.findtext('name') for ev in f.iter('enumeratedValue')}
    d.pop(31, None)                                  # 'null'
    rows[int(m.group(1))] = d
    slots |= set(d)

slots = sorted(slots)
with open(OUT, 'w', newline='') as fh:
    w = csv.writer(fh)
    w.writerow(['gp'] + ['f%d' % s for s in slots] + ['adc'])
    for gp in sorted(rows):
        adc = gp - ADC_BASE if ADC_BASE <= gp < ADC_BASE + ADC_N else ''
        w.writerow([gp] + [rows[gp].get(s, '') for s in slots] + [adc])
print('%d Pins, Funktionen f%s -> %s' % (len(rows), '/f'.join(map(str, slots)), OUT))
