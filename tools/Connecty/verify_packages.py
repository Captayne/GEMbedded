"""Checks downloads, cache validation and merge output without touching hardware."""
import sys
import tempfile
from pathlib import Path
import workbench_core as core

if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    for entry in core.CATALOG['entries']:
        if not entry.get('url'):
            continue
        prepared = core.prepare(entry, core.ROOT / 'firmware', print)
        second = core.prepare(entry, core.ROOT / 'firmware', print)
        assert prepared['receipt'] == second['receipt']
        plan = prepared['plan']
        print(plan['title'], 'segments:', len(plan['segments']), 'options:', plan.get('build_options'))
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / 'merged.bin'
            args = ['merge-bin', '--output', str(target), '--fill-flash-size', str(plan['flash_bytes'] // 1048576) + 'MB']
            for k, v in plan['options'].items():
                args += ['--' + k.replace('_', '-'), v]
            for part in plan['segments']:
                args += [part['offset'], str(Path(prepared['directory']) / part['file'])]
            core.run_tool('UNUSED', args, print, chip=plan['chip'])
            assert target.stat().st_size == plan['flash_bytes']
        print('DOWNLOAD / CACHE / MERGE OK')
