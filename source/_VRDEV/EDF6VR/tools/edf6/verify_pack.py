"""Check every archive the pack has written against the one it was made from.

This is what says a pack is safe to load before the game ever sees it. For each
written file: the same records in the same order with the same names, flags and
directories; every untouched block still byte for byte what shipped; every
changed block a real DDS of exactly twice the size in exactly the same format;
the two buffer-size fields matching what is actually in the file; and the blocks
filling the file with no gap and no overlap.

--pictures adds a slower pass that decodes every enlarged texture and compares
it with the one it came from. It is separate because it costs a decode, and it
exists because the structural pass cannot see what it catches: an upscaler that
quietly returns solid black produces an archive in which every field is right.

    python tools/edf6/verify_pack.py [game directory] [--pictures]
"""
import os
import shutil
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cpk as cpk_module
import rab as rab_module
import dds as dds_module
import check as check_module
import hd_textures as hd_module


def full_chain(width, height):
    """Mip levels from the full size down to one pixel."""
    levels = 1
    while width > 1 or height > 1:
        width = max(1, width // 2)
        height = max(1, height // 2)
        levels += 1
    return levels


def check_pictures(before, after, label, faults, tools, work):
    """Every enlarged texture still looks like the one it came from.

    Separate from the structural pass because it costs a decode. It exists
    because the structural pass cannot see it: an upscaler that returns solid
    black produces an archive in which every field is correct.
    """
    sources = os.path.join(work, 'verify_src')
    results = os.path.join(work, 'verify_new')
    # Emptied rather than removed and remade: on Windows the delete completes
    # after rmtree returns, and the next mkdir fails on a directory that is on
    # its way out.
    for folder in (sources, results):
        os.makedirs(folder, exist_ok=True)
        for name in os.listdir(folder):
            os.remove(os.path.join(folder, name))
    stems = []
    for index, (old, new) in enumerate(zip(before.entries, after.entries)):
        old_info, new_info = dds_module.read(old.head()), dds_module.read(new.head())
        if not (old_info.ok and new_info.ok):
            continue
        if (old_info.width, old_info.height) == (new_info.width, new_info.height):
            continue
        stem = 'v%05d' % index
        stems.append((stem, old.name))
        open(os.path.join(sources, stem + '.dds'), 'wb').write(old.data)
        open(os.path.join(results, stem + '.dds'), 'wb').write(new.data)
    if not stems:
        return 0
    kept, lost = check_module.survivors(tools, sources, results, work,
                                        [stem for stem, _ in stems])
    named = dict(stems)
    for stem in lost:
        faults.append('%s: %s came back far darker than it was' % (label, named[stem]))
    return len(kept) + len(lost)


def check(original_blob, new_blob, label, faults):
    before = rab_module.parse(original_blob)
    after = rab_module.parse(new_blob)

    def fault(text):
        faults.append('%s: %s' % (label, text))

    if len(before.entries) != len(after.entries):
        fault('%d records became %d' % (len(before.entries), len(after.entries)))
        return 0
    if before.directories != after.directories:
        fault('directory list changed')
    if before.sorted_order != after.sorted_order:
        fault('name index order changed')

    resized = 0
    for old, new in zip(before.entries, after.entries):
        if (old.name, old.directory, old.flags) != (new.name, new.directory, new.flags):
            fault('record changed for %s' % old.name)
            continue
        old_info = dds_module.read(old.head())
        new_info = dds_module.read(new.head())
        if (old_info.width, old_info.height) == (new_info.width, new_info.height):
            if old.stored != new.stored:
                fault('%s was rewritten but is the same size' % old.name)
            continue
        resized += 1
        if not new_info.ok:
            fault('%s is no longer a DDS we can read' % old.name)
        elif new_info.width != old_info.width * 2 or new_info.height != old_info.height * 2:
            fault('%s went %dx%d -> %dx%d, not double'
                  % (old.name, old_info.width, old_info.height, new_info.width, new_info.height))
        elif new_info.format != old_info.format or new_info.dx10 != old_info.dx10:
            fault('%s changed format %s -> %s' % (old.name, old_info.format, new_info.format))
        # A full chain, which is what texconv -m 0 makes. Not old + 1: the game
        # ships at least one texture whose own chain stops a level early
        # (OBJECT/V510_MASER.MRAB, v510_maserdf02.dds is 1024x512 with ten
        # levels where eleven is full), so matching the original exactly would
        # mean reproducing its omission rather than checking anything.
        elif new_info.mips != full_chain(new_info.width, new_info.height):
            fault('%s has %d mip levels, a full chain is %d'
                  % (old.name, new_info.mips, full_chain(new_info.width, new_info.height)))
        elif new_info.mips < old_info.mips:
            fault('%s lost mip levels, %d -> %d' % (old.name, old_info.mips, new_info.mips))

    largest_stored, largest_plain = struct.unpack_from('<II', new_blob, 0x0C)
    if largest_stored != max(len(e.stored) for e in after.entries):
        fault('largest stored block field is wrong')
    if largest_plain != max(e.plain_size for e in after.entries):
        fault('largest decompressed block field is wrong')

    count = struct.unpack_from('<I', new_blob, 0x14)[0]
    records_at = struct.unpack_from('<I', new_blob, 0x18)[0]
    spans = []
    for index in range(count):
        record = records_at + 32 * index
        size = struct.unpack_from('<I', new_blob, record + 4)[0]
        offset = struct.unpack_from('<Q', new_blob, record + 24)[0]
        spans.append((offset, offset + size))
    spans.sort()
    start = struct.unpack_from('<I', new_blob, 0x08)[0]
    for low, high in spans:
        if low != start:
            fault('gap or overlap in the block area at %d' % low)
            break
        start = high
    else:
        if start != len(new_blob):
            fault('file is %d bytes but the blocks end at %d' % (len(new_blob), start))
    return resized


def main():
    arguments = [a for a in sys.argv[1:] if a and a != '--pictures']
    pictures = '--pictures' in sys.argv[1:]
    root = arguments[0] if arguments else hd_module.find_game(
        os.path.dirname(os.path.abspath(__file__)))
    mods = os.path.join(root, 'Mods')
    record = os.path.join(mods, 'HDTextureWork', 'written.txt')
    if not os.path.isfile(record):
        raise SystemExit('no pack has been built yet (%s)' % record)

    sources = [cpk_module.Cpk(os.path.join(root, n))
               for n in ('Root.cpk', 'Chunk01.cpk', 'Chunk02.cpk')
               if os.path.isfile(os.path.join(root, n))]

    faults = []
    checked = resized = 0
    for line in open(record, encoding='utf-8').read().split():
        path = os.path.join(root, line.replace('/', os.sep))
        if not os.path.isfile(path):
            continue
        parts = line.split('/')
        directory, name = parts[-2], parts[-1]
        original = None
        for source in sources:
            if (directory, name) in source.index:
                original = source.read(directory, name)
                break
        if original is None:
            faults.append('%s: no original in any archive' % line)
            continue
        new_blob = open(path, 'rb').read()
        resized += check(original, new_blob, line, faults)
        if pictures:
            check_pictures(rab_module.parse(original), rab_module.parse(new_blob), line,
                           faults, hd_module.Tools(hd_module.find_tools(
                               os.path.dirname(os.path.abspath(__file__)))),
                           os.path.join(mods, 'HDTextureWork'))
        checked += 1
        print('  checked %s' % line)

    print('%d archives checked, %d textures doubled' % (checked, resized))
    for text in faults:
        print('FAULT %s' % text)
    print('PASS' if not faults else 'FAILED (%d)' % len(faults))
    return 1 if faults else 0


if __name__ == '__main__':
    sys.exit(main())
