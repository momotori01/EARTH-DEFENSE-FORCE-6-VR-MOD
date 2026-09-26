"""Builds assets/LYT_MAINFRAME.SGO: the game's menu frame layout plus one empty text field
(MSLabel, lower left) that EDF6MultiSlot.dll fills with "F2 8Player MOD :ON/OFF". Read-only on the game:
the layout is read from Root.cpk and written into this project only.

  python -B tools/make_menu_label.py [out-path]

The build embeds the file in the plugin, which writes it to Mods/UI/LYT_MAINFRAME.SGO while it is active
and removes it otherwise (src/menulayout.cpp). EDFModLoader's Mods folder redirector (ModLoader.ini
Redirect=True) makes the game load it instead of the archived file. Without the plugin the field stays
a single space, so the layout looks exactly like the game's.
When the output changes, add its size and FNV-1a 64 to kKnownLayouts in src/menulayout.cpp.
"""
import hashlib, os, pathlib, sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from cpk import Cpk  # noqa: E402
import crilayla  # noqa: E402
import sgo_write  # noqa: E402

GAME = HERE.parents[1]
# UI/LYT_MAINFRAME.SGO of the install this was made for (EDF.dll build 678CCB46).
ORIGINAL_SHA256 = '1a1d6dc5653cf46468556ce154899381b8c108b82064990402b07090192eb7cb'


def original_layout():
    archive = Cpk(os.path.join(GAME, 'Root.cpk'))
    entry = archive.index[('UI', 'LYT_MAINFRAME.SGO')]
    with open(archive.path, 'rb') as handle:
        handle.seek(archive.base + int(entry['FileOffset']))
        data = handle.read(int(entry['FileSize']))
    if int(entry['ExtractSize']) != int(entry['FileSize']):
        data = crilayla.decompress(data)
    return data


def main():
    out = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else HERE.parent / 'assets' / 'LYT_MAINFRAME.SGO'
    data = original_layout()
    if hashlib.sha256(data).hexdigest() != ORIGINAL_SHA256:
        raise SystemExit('Root.cpk has a different UI/LYT_MAINFRAME.SGO than the one this label was made for')
    values = sgo_write.parse(data)
    if sgo_write.write(values) != data:
        raise SystemExit('the writer does not reproduce the original layout; refusing to build')
    result = sgo_write.write(sgo_write.add_mainframe_label(values))
    check = dict(sgo_write.parse(result))
    original = dict(values)
    for name, node in original.items():
        if name != 'layout_tree' and check[name] != node:
            raise SystemExit(f'{name} changed')
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(result)
    print(f'original {len(data)} bytes sha256 {hashlib.sha256(data).hexdigest()}')
    fnv = 0xCBF29CE484222325
    for byte in result:
        fnv = ((fnv ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    print(f'wrote {out} ({len(result)} bytes, sha256 {hashlib.sha256(result).hexdigest()}, fnv1a64 {fnv:#018x})')


if __name__ == '__main__':
    main()
