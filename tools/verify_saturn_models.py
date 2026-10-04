#!/usr/bin/env python3
"""verify_saturn_models.py - check the Saturn entity-model port against the disc.

`src/game/saturn/SaturnModels.cpp` transcribes two facts read off the Sega
Saturn disc (docs/SATURN_VERSION_ANALYSIS.md 4g) that are not in this repo, plus the
build wiring that keeps them from being silently dropped:

1. **The model table.** The Saturn's per-character EMD name table (GAME5
   0x0609AD5C) is the same 53-entry-per-block shape as the port's
   g_emdPathTable, and it fills two of the ten `em100a` placeholder slots the PC
   leaves at indices 26-35: index 26 (type 0x16) is `em1016`/`em1116` and index
   27 (type 0x17) is `em1017`/`em1117`. Index 35 would be `em1018`/`em1118`,
   but that model is NOT on the disc, so nothing may override it. This reads the
   port's own table to confirm 26 and 27 really are the fillers being claimed
   (in BOTH character blocks - a silent off-by-block would point Jill's Tick at
   the Hunter file), and confirms the paths the port returns exist in the
   `assets/SATURN/Enemy/` overlay.

2. **The Tick loader swap.** The Saturn swaps the Hunter's model for the Tick's
   on stage index 2 (GAME5 0x0604EA80); the entity keeps type 6. This checks
   that the guard is exactly (model index 10, stage 2) - no wider and no
   narrower, since either error is invisible in the campaign and wrong in the
   Battle Game.

3. **The wiring.** Both build systems must list the file, every call site must
   be gated on g_bSaturnMode so OG and DC can never reach it, and no two sources
   in Game.vcxproj may share a basename. That last one is not hypothetical: MSVC
   derives object names from the basename alone, so `dc/EntityModels.cpp` and
   `saturn/EntityModels.cpp` both produced `obj/Release/EntityModels.obj`,
   MSB8027 warned, the Saturn object silently overwrote the DC one, and the link
   failed with four missing `dc_*` symbols. It is why the file is SaturnModels.*.

Everything is local - no Ghidra bridge and no disc image needed, because the
Saturn values are transcribed constants rather than something read live.

    python tools/verify_saturn_models.py
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

LOADER = os.path.join(REPO, 'src', 'game', 'EntityModelLoader.cpp')
SATURN_SRC = os.path.join(REPO, 'src', 'game', 'saturn', 'SaturnModels.cpp')
SATURN_HDR = os.path.join(REPO, 'src', 'game', 'saturn', 'SaturnModels.h')
SATURN_ENEMY = os.path.join(REPO, 'assets', 'SATURN', 'Enemy')
VCXPROJ = os.path.join(REPO, 'Game.vcxproj')
CMAKELISTS = os.path.join(REPO, 'CMakeLists.txt')

BLOCK_ENTRIES = 53

# What the Saturn's table holds at the indices it claims, as
# (model index, entity type, Chris file, Jill file).
EXPECTED = [
    (26, 0x16, 'EM1016.EMD', 'EM1116.EMD'),   # the Tick
    (27, 0x17, 'EM1017.EMD', 'EM1117.EMD'),   # Zombie Wesker
]

# The one index the Saturn names but does not ship: type 0x1F, model index 35.
NOT_ON_DISC = (35, 'EM1018.EMD')

# The Tick loader swap (GAME5 0x0604EA80).
TICK_FROM_INDEX = 10   # the Hunter, em1006 - entity type 6 + 4
TICK_STAGE = 2         # 0-indexed stage, the mansion main hall

failures = []
skips = []


def ok(msg):
    print('  ok   %s' % msg)


def fail(msg):
    failures.append(msg)
    print('  FAIL %s' % msg)


def check(cond, msg):
    if cond:
        ok(msg)
    else:
        fail(msg)
    return cond


def port_emd_table(src):
    """The port's g_emdPathTable as a list of 106 file names, or None."""
    m = re.search(r'g_emdPathTable\[\d+\]\[\d+\]\s*=\s*\{(.*?)\n\};', src, re.S)
    if not m:
        return None
    return re.findall(r'"([^"]+)"', m.group(1))


def check_claimed_slots_are_fillers(port_table, index, label):
    """The Saturn claims a slot the PC leaves as a placeholder.

    Both character blocks are checked: the table is addressed as
    `blockBase + index`, and a wrong block base would resolve Jill's Tick
    through the Chris block's row - which holds the Hunter - so the model would
    load and look almost right. The placeholder itself is per-block: the Chris
    block's fillers are `em100a` (indices 26-35) and Jill's are `em110a`
    (indices 79-88), the same 1x/11x split every other entry follows.
    """
    for block, filler in ((0, 'enemy/em100a.emd'), (1, 'enemy/em110a.emd')):
        entry = port_table[block * BLOCK_ENTRIES + index]
        check(entry.lower() == filler,
              'port table block %d index %d is %s (the %s placeholder %s claims)'
              % (block, index, entry, filler, label))


def check_model_table(saturn_src, port_table):
    for index, entity_type, chris, jill in EXPECTED:
        check(index == entity_type + 4,
              'index %d is entity type 0x%02X + 4' % (index, entity_type))
        check_claimed_slots_are_fillers(port_table, index,
                                        'index %d' % index)

        # The port returns the two names, one per block, lower case, the way
        # every other entry in g_emdPathTable is spelled. The `jillBlock ?` arm
        # comes first, so the regex captures (jill, chris).
        pattern = r'case\s+SATURN_EMD_INDEX_\w+\s*:\s*return\s+jillBlock\s*\?\s*'
        matches = re.findall(pattern +
                             r'"([^"]+)"\s*:\s*"([^"]+)"\s*;', saturn_src)
        wanted = ('enemy/' + jill.lower(), 'enemy/' + chris.lower())
        check(wanted in matches,
              'index %d returns %s on the Jill block, %s on the Chris block'
              % (index, wanted[0], wanted[1]))

    # Type 0x1F / index 35 is named in the table but absent from the disc, so
    # the port must leave the placeholder alone rather than point at a file that
    # is not there.
    index, missing = NOT_ON_DISC
    named = re.search(r'case\s+%d\s*:' % index, saturn_src)
    check(named is None,
          'index %d (%s) is NOT overridden - the model is not on the disc'
          % (index, missing))


def check_tick_swap(saturn_src, saturn_hdr):
    m = re.search(r'#define\s+SATURN_EMD_INDEX_HUNTER\s+(\d+)', saturn_hdr)
    check(m is not None and int(m.group(1)) == TICK_FROM_INDEX,
          'SATURN_EMD_INDEX_HUNTER is %d (the Hunter, em1006)' % TICK_FROM_INDEX)

    m = re.search(r'#define\s+SATURN_TICK_STAGE\s+(\d+)', saturn_hdr)
    check(m is not None and int(m.group(1)) == TICK_STAGE,
          'SATURN_TICK_STAGE is %d (0-indexed, the mansion main hall)'
          % TICK_STAGE)

    # The guard has to be exactly the two conditions and nothing else: widened,
    # every Hunter in the mansion becomes a Tick; narrowed, stage 3's are not.
    guard = re.search(r'if\s*\((.*?)\)\s*\{\s*return\s+SATURN_EMD_INDEX_TICK;',
                      saturn_src, re.S)
    if not check(guard is not None,
                 'saturn_emd_model_index() returns SATURN_EMD_INDEX_TICK'):
        return
    body = ' '.join(guard.group(1).split())
    check(body == 'modelIndex == SATURN_EMD_INDEX_HUNTER && '
                  'g_stageId == SATURN_TICK_STAGE',
          'the swap guard is exactly (index %d, stage %d): %r'
          % (TICK_FROM_INDEX, TICK_STAGE, body))


def check_wiring(loader_src):
    # Every Saturn call site in the loader must sit under a g_bSaturnMode test,
    # or OG and DC sessions would take the Saturn branch.
    for fn in ('saturn_emd_model_index', 'saturn_emd_path'):
        m = re.search(r'if\s*\(g_bSaturnMode\)\s*\{[^}]*' + fn, loader_src)
        check(m is not None,
              'EntityModelLoader calls %s() under g_bSaturnMode' % fn)

    check('#include "saturn/SaturnModels.h"' in loader_src,
          'EntityModelLoader includes saturn/SaturnModels.h')


def check_build_files():
    for path, needle, label in (
            (VCXPROJ, 'src\\game\\saturn\\SaturnModels.cpp', 'Game.vcxproj'),
            (CMAKELISTS, 'src/game/saturn/SaturnModels.cpp', 'CMakeLists.txt')):
        text = open(path, encoding='utf-8', errors='replace').read()
        check(needle in text, '%s lists src/game/saturn/SaturnModels.cpp' % label)

    # The MSB8027 trap: MSVC names objects after the BASENAME, so two sources
    # with one basename share an .obj and one silently overwrites the other.
    # See the header of src/game/saturn/SaturnModels.h.
    vcx = open(VCXPROJ, encoding='utf-8', errors='replace').read()
    sources = re.findall(r'<ClCompile Include="([^"]+)"', vcx)
    by_name = {}
    for s in sources:
        by_name.setdefault(os.path.basename(s), []).append(s)
    dupes = {n: v for n, v in by_name.items() if len(v) > 1}
    if dupes:
        for name, group in sorted(dupes.items()):
            fail('%s.cpp basename collides in Game.vcxproj: %s - they share one'
                 ' .obj and one overwrites the other'
                 % (name, ', '.join(group)))
    else:
        ok('all %d Game.vcxproj sources have distinct basenames' % len(sources))

    cml = open(CMAKELISTS, encoding='utf-8', errors='replace').read()
    cml_sources = re.findall(r'^\s*(\S+\.cpp)\s*$', cml, re.M)
    print('   and CMakeLists lists %d sources' % len(cml_sources))


def check_overlay_files(saturn_src):
    print('   and the models it names are in assets/SATURN/Enemy')
    if not os.path.isdir(SATURN_ENEMY):
        skips.append('no assets/SATURN/Enemy')
        print('  SKIP no %s' % SATURN_ENEMY)
        return
    have = {n.lower() for n in os.listdir(SATURN_ENEMY)}

    # Every name the port can actually return, taken from the source rather than
    # from EXPECTED, so a newly added creature is checked too.
    wanted = sorted({os.path.basename(m) for m in
                     re.findall(r'"enemy/(em[0-9a-f]+\.emd)"', saturn_src)})
    if not wanted:
        fail('no enemy model paths found in SaturnModels.cpp')
        return
    for name in wanted:
        if name.lower() in have:
            ok('assets/SATURN/Enemy/%s' % name)
        else:
            fail('assets/SATURN/Enemy/%s is missing - Saturn mode would fall back'
                 ' to the base tree, which does not have it either' % name)


def main():
    loader_src = open(LOADER, encoding='utf-8', errors='replace').read()
    saturn_src = open(SATURN_SRC, encoding='utf-8', errors='replace').read()
    saturn_hdr = open(SATURN_HDR, encoding='utf-8', errors='replace').read()

    port_table = port_emd_table(loader_src)
    if not check(port_table is not None,
                 'read the port\'s g_emdPathTable'):
        return 1
    check(len(port_table) == BLOCK_ENTRIES * 2,
          'the port\'s g_emdPathTable has %d entries (%d per block)'
          % (len(port_table), BLOCK_ENTRIES))

    check_model_table(saturn_src, port_table)
    check_tick_swap(saturn_src, saturn_hdr)
    check_wiring(loader_src)
    check_build_files()
    check_overlay_files(saturn_src)

    print()
    for s in skips:
        print('skipped: %s' % s)
    if failures:
        print('%d check(s) FAILED' % len(failures))
        return 1
    print('ok: the Saturn entity-model port matches the disc')
    return 0


if __name__ == '__main__':
    sys.exit(main())
