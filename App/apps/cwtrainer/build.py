"""Portable ARM build. --toolchain points to the ARM GCC bin directory."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import zlib

p = argparse.ArgumentParser()
p.add_argument('--toolchain', type=Path)
args = p.parse_args()
here = Path(__file__).resolve().parent
os.chdir(here)
for stale in here.glob('*.su'):
    stale.unlink()  # Generated diagnostics from previous/LTO experiments only.
subprocess.run([sys.executable,'gen_assets.py'],check=True)
vma = os.environ.get('APP_VMA', '0x20000280')

def tool(name):
    return str(args.toolchain / name) if args.toolchain else name

cc = os.environ.get('CC', tool('arm-none-eabi-gcc'))
objcopy = os.environ.get('OBJCOPY', tool('arm-none-eabi-objcopy'))
command = [cc, '-mcpu=cortex-m0plus', '-mthumb', '-Os', '-fno-jump-tables',
           '-fno-guess-branch-probability',
           '-std=gnu11', '-ffreestanding', '-fno-builtin', '-fno-common',
           '-fomit-frame-pointer', '-ffunction-sections', '-fdata-sections',
           '-fstack-usage', '-Wall', '-Wextra', '-Werror', '-nostdlib',
           '-nostartfiles', '-T', 'app.ld', f'-Wl,--defsym,APP_VMA={vma}',
           '-Wl,--gc-sections,-Map=cwtrainer.map,--build-id=none,--no-warn-rwx-segments',
           'cwtrainer_app.c', '-lgcc', '-o', 'cwtrainer.elf']
subprocess.run(command, check=True)
subprocess.run([objcopy, '-O', 'binary', 'cwtrainer.elf', 'cwtrainer.bin'], check=True)
subprocess.run([sys.executable, '../pack_app.py', 'cwtrainer.bin', 'CWTrainer.app',
                '--name', 'CW Trainer', '--ver', '0.3.3', '--api-min', '3',
                '--vma', vma, '--shortcut', 'none','--assets','cwtrainer_assets.bin'], check=True)
blob = (here / 'CWTrainer.app').read_bytes()
size, crc = struct.unpack_from('<II', blob, 8)
assets=struct.unpack_from('<H', blob, 60)[0]
assert len(blob) == size + 64 + assets and size <= 4096
assert zlib.crc32(blob[64:64+size]) == crc
assert (zlib.crc32(blob[64+size:]) & 0xffff)==struct.unpack_from('<H',blob,62)[0]
mapping = (here / 'cwtrainer.map').read_text()
start = int(re.search(r'0x([0-9a-f]+)\s+__app_bss_start', mapping)[1], 16)
end = int(re.search(r'0x([0-9a-f]+)\s+__app_bss_end', mapping)[1], 16)
undefined = subprocess.check_output([tool('arm-none-eabi-nm'), '-u', 'cwtrainer.elf'], text=True)
assert not undefined.strip(), undefined
report = {'version': '0.3.3', 'vma': vma, 'overlay_bytes': size,
          'code_crc32': f'{crc:08x}',
          'bss_bytes': end-start, 'overlay_free_bytes': 4096-size,
          'assets_bytes': assets, 'package_bytes': len(blob),
          'sha256': hashlib.sha256(blob).hexdigest(),
          'compiler': subprocess.check_output([cc, '--version'], text=True).splitlines()[0],
          'command': command, 'unresolved_symbols': []}
(here / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({k:v for k,v in report.items() if k not in ('command',)}, indent=2))
