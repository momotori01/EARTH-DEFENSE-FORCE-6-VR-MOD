"""Little-endian SGO (magic 'SGO\\0', version 0x102) round-trip writer. Read-only on game data.

  python -B tools/sgo_write.py roundtrip FILE.SGO
      parse FILE and write it back; reports whether the bytes are identical
  python -B tools/sgo_write.py mainframe-label LYT_MAINFRAME.SGO OUT.SGO
      adds the MultiSlot menu label (component MSLabel) to the main frame layout

Nodes are 12 bytes {int type, int a, uint b}: 0 array (a = count, b = offset from the node to its
children), 1 int (b = value), 2 float (b = bits), 3 string (a = length, b = offset to UTF-16), 4 raw
(a = size, b = offset). Top-level values are sorted by name (ordinal); the name table holds
{offset from the entry to the name, value index}. Child blocks follow the top-level table in
depth-first order, then the name table and one pool of names and string values (sorted, each once).
"""
import struct, sys


class Node:
    __slots__ = ('kind', 'value')

    def __init__(self, kind, value):
        self.kind = kind    # 'arr' | 'int' | 'flt' | 'str' | 'raw'
        self.value = value  # list[Node] | int | int (float bits) | str | bytes

    def __eq__(self, other):
        return isinstance(other, Node) and self.kind == other.kind and self.value == other.value

    def __repr__(self):
        if self.kind == 'flt':
            return f'{struct.unpack("<f", struct.pack("<I", self.value))[0]:g}f'
        return repr(self.value)


def Arr(*items): return Node('arr', list(items))
def Int(v): return Node('int', int(v))
def Flt(v): return Node('flt', struct.unpack('<I', struct.pack('<f', float(v)))[0])
def Str(s): return Node('str', s)


def _utf16(buf, off):
    end = off
    while buf[end:end + 2] != b'\0\0':
        end += 2
    return buf[off:end].decode('utf-16le')


def parse(buf):
    magic, version, count, data_off, name_count, name_off = struct.unpack_from('<4sIIIII', buf, 0)
    if magic != b'SGO\0' or version != 0x102:
        raise ValueError(f'not a little-endian SGO 0x102 file ({magic!r}, {version:#x})')

    def node(pos):
        t, a, b = struct.unpack_from('<iiI', buf, pos)
        if t == 0:
            return Node('arr', [node(pos + b + i * 12) for i in range(a)])
        if t == 1:
            return Node('int', struct.unpack('<i', struct.pack('<I', b))[0])
        if t == 2:
            return Node('flt', b)
        if t == 3:
            return Node('str', _utf16(buf, pos + b) if a else '')
        if t == 4:
            return Node('raw', buf[pos + b:pos + b + a])
        raise ValueError(f'unknown node type {t} at {pos:#x}')

    names = {}
    for i in range(name_count):
        rel, index = struct.unpack_from('<iI', buf, name_off + i * 8)
        names[index] = _utf16(buf, name_off + i * 8 + rel)
    return [(names[i], node(data_off + i * 12)) for i in range(count)]


def write(values):
    values = sorted(values, key=lambda kv: [ord(c) for c in kv[0]])
    count = len(values)
    area = bytearray(count * 12)  # node area, starting at file offset 0x20
    strings = []                  # (node offset in the file, text)
    raws = []                     # (node offset in the file, bytes)

    def emit(pos, n):
        off = pos - 0x20
        if n.kind == 'arr':
            struct.pack_into('<iiI', area, off, 0, len(n.value), 0)
        elif n.kind == 'int':
            struct.pack_into('<iiI', area, off, 1, 4, n.value & 0xFFFFFFFF)
        elif n.kind == 'flt':
            struct.pack_into('<iiI', area, off, 2, 4, n.value)
        elif n.kind == 'str':
            struct.pack_into('<iiI', area, off, 3, len(n.value), 0)
            strings.append((pos, n.value))
        elif n.kind == 'raw':
            struct.pack_into('<iiI', area, off, 4, len(n.value), 0)
            raws.append((pos, n.value))
        else:
            raise ValueError(n.kind)

    def layout(pos, n):
        if n.kind != 'arr':
            return
        block = 0x20 + len(area)
        struct.pack_into('<I', area, pos - 0x20 + 8, block - pos)
        area.extend(bytes(len(n.value) * 12))
        for i, child in enumerate(n.value):
            emit(block + i * 12, child)
        for i, child in enumerate(n.value):
            layout(block + i * 12, child)

    for i, (_, n) in enumerate(values):
        emit(0x20 + i * 12, n)
    for i, (_, n) in enumerate(values):
        layout(0x20 + i * 12, n)

    out = bytearray(0x20) + area
    name_off = len(out)
    out += bytes(count * 8)
    unk_off = len(out)
    for pos, data in raws:
        struct.pack_into('<I', out, pos + 8, len(out) - pos)
        out += data
    # One pool for names and string values, sorted by UTF-16 code unit, each text once.
    pool = sorted({name for name, _ in values} | {text for _, text in strings}, key=lambda t: [ord(c) for c in t])
    placed = {}
    for text in pool:
        placed[text] = len(out)
        out += text.encode('utf-16le') + bytes(2)
    for i, (name, _) in enumerate(values):
        entry = name_off + i * 8
        struct.pack_into('<iI', out, entry, placed[name] - entry, i)
    for pos, text in strings:
        struct.pack_into('<I', out, pos + 8, placed[text] - pos)
    struct.pack_into('<4sIIIIIII', out, 0, bytes([0x53, 0x47, 0x4F, 0]), 0x102, count, 0x20, count, name_off, 0, unk_off)
    return bytes(out)


def add_mainframe_label(values):
    """MSLabel: a TextField in the lower left of the main frame, filled in by EDF6MultiSlot.dll."""
    table = dict(values)
    if 'MSLabel' in table:
        raise ValueError('layout already has MSLabel')
    tree = table['layout_tree']
    # layout_tree = [['MainFrame'], [children as name-list / 0 pairs]]
    if not (tree.kind == 'arr' and len(tree.value) == 2 and tree.value[0] == Arr(Str('MainFrame'))):
        raise ValueError('unexpected layout_tree shape')
    children = tree.value[1]
    children.value.extend([Arr(Str('MSLabel')), Int(0)])
    label = Arr(
        Str('TextField'), Str('app:/UI/Transparent_skin.sgo'),
        Arr(Flt(192.0), Flt(970.0), Flt(0.0)), Arr(Flt(0.0), Flt(0.0), Flt(640.0), Flt(28.0)),
        Arr(), Int(0), Int(0),
        Arr(Arr(Str('text_dr'), Str(' ')), Arr(Str('font_size'), Arr(Int(22), Int(22))),
            Arr(Str('font_border_width'), Flt(2.0))))
    return values + [('MSLabel', label)]


if __name__ == '__main__':
    command = sys.argv[1]
    data = open(sys.argv[2], 'rb').read()
    values = parse(data)
    if command == 'roundtrip':
        again = write(values)
        print('identical' if again == data else f'different ({len(again)} vs {len(data)} bytes)')
        assert parse(again) == values, 'parsed values differ after writing'
    elif command == 'mainframe-label':
        result = write(add_mainframe_label(values))
        check = dict(parse(result))
        assert check['MSLabel'] == dict(add_mainframe_label(parse(data)))['MSLabel']
        open(sys.argv[3], 'wb').write(result)
        print(f'wrote {sys.argv[3]} ({len(result)} bytes)')
