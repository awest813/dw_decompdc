#!/usr/bin/env python3
"""Post-process a compiled game object for the port build.

Game code is compiled with -fdata-sections, so every variable has its own
section. This script then:

1. Raises the alignment of every data section to at least 4 bytes. The
   original build places each top-level variable on a 4-byte boundary;
   GCC would use the type's alignment (1 or 2 for byte and short arrays).
   That changes which variables are neighbours, and word accesses the
   original code does through casts would fault on SH-4. qemu does not
   trap misaligned accesses, so only real hardware would show it.

2. For overlay objects (--overlay NAME), renames the .data/.bss sections
   to dw_ovl_NAME_data / dw_ovl_NAME_bss. The linker then groups each
   overlay's data and defines __start_/__stop_ bounds, which the port
   runtime uses to restore the data when the game loads the overlay.
"""

import argparse
import re
import subprocess
import sys

MIN_ALIGN = 4

# Allocated data sections. Mergeable constants (.rodata.str*, .rodata.cst*)
# are anonymous and keep their own alignment.
DATA_RE = re.compile(r'^\.(data|bss|rodata)(\..+)?$')
MERGEABLE_RE = re.compile(r'^\.rodata\.(str|cst)')


def sections(readelf, obj):
    out = subprocess.run([readelf, '-SW', obj], check=True,
                         capture_output=True, text=True).stdout
    result = []
    for line in out.splitlines():
        # "  [ 4] .data.a  PROGBITS  00000000 000034 000003 00  WA  0  0  4"
        # The name follows "]" and the alignment is the last column.
        m = re.match(r'\s*\[\s*\d+\]\s+(.*)$', line)
        if not m:
            continue
        fields = m.group(1).split()
        if len(fields) >= 8 and fields[-1].isdigit():
            result.append((fields[0], int(fields[-1])))
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--readelf', required=True)
    p.add_argument('--objcopy', required=True)
    p.add_argument('--overlay', help='overlay name for data grouping')
    p.add_argument('object')
    args = p.parse_args()

    secs = sections(args.readelf, args.object)
    if not secs:
        sys.exit(f'fix_obj: could not read sections of {args.object}')

    cmd = [args.objcopy]
    for name, align in secs:
        if not DATA_RE.match(name) or MERGEABLE_RE.match(name):
            continue
        if align < MIN_ALIGN:
            cmd += ['--set-section-alignment', f'{name}={MIN_ALIGN}']
        if args.overlay:
            kind = 'bss' if name.startswith('.bss') else DATA_RE.match(name).group(1)
            if kind in ('data', 'bss'):
                cmd += ['--rename-section', f'{name}=dw_ovl_{args.overlay}_{kind}']
    if len(cmd) > 1:
        subprocess.run(cmd + [args.object], check=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
