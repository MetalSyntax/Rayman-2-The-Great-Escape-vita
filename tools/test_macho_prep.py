#!/usr/bin/env python3
"""Regresión del rebase (tools/macho_prep.py) con casos vistos en consola y en el análisis del binario stripped.

Uso: python3 -I tools/test_macho_prep.py [Executable]   (por defecto game_app/Rayman2)
"""
import os, struct, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'game_app', 'Rayman2')

CASES = {
    # Rayman2 (armv6, mixed non-PIC/PIC code). Evidence: tools/annotate.py dumps, 2026-11-04.
    0x6358: True,    # _UpdateButton: switch table after `ldrls pc, [pc, r3, lsl #2]` (0x6350)
    0x6370: True,    # last case of that table
    0x6374: False,   # first instruction after the table
    0x2D68: True,    # applicationDidFinishLaunching: non-PIC literal -> @selector(currentDevice) selref
    0x2D84: True,    # non-PIC literal -> __nl_symbol_ptr slot of _g_mach_timebase_info
    0x6848: True,    # _updateButtons literal -> _DpadButton (__data)
    0x2D5C: True,    # SjLj resume address (function-internal label) in applicationDidFinishLaunching:
    0xC17C8: False,  # CBGMPlayer::LoadFile: PIC offset (ldr r0, [pc, r3])
    0xC17CC: False,  # FourCC 'dfmt' literal (AudioFileGetProperty id)
    0xC1A0C: False,  # mask 0x00ffff00 in QueueCallback
    0x6B58: False,   # _TouchpadInit numeric literal 310 (button y1)
    0x6B5C: False,   # _TouchpadInit numeric literal 478 (button x1)
    0x8C340: False,  # hosttest crash: GLI_vFogOff offset 0x2d3f4 used as `str r2, [r3, r1]` index
    0x8C33C: False,  # same function, offset 0x2d3f0
}

with tempfile.TemporaryDirectory() as d:
    out = os.path.join(d, 'rebase.bin')
    subprocess.run([sys.executable, '-I', os.path.join(ROOT, 'tools', 'macho_prep.py'), BIN, out],
                   check=True, stdout=subprocess.DEVNULL)
    b = open(out, 'rb').read()
    rebase = set(struct.unpack_from(f'<{struct.unpack_from("<I", b, 4)[0]}I', b, 8))
bad = [f'{a:#x} esperado {"en" if want else "fuera de"} rebase.bin' for a, want in CASES.items() if (a in rebase) != want]
print('\n'.join(bad) or f'macho_prep ok ({len(CASES)} casos, {len(rebase)} palabras)')
sys.exit(1 if bad else 0)
