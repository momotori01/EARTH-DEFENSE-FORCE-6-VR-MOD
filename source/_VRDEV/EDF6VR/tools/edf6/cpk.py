"""Read-only CRI CPK reader for the archives EDF6 ships.

The game binds Root.cpk, Chunk01.cpk, Chunk02.cpk and DX11.cpk (the names are
wide strings in EDF.dll) and asks for everything through them. Nothing here
writes: the game's own files are opened 'rb' and never anything else, because
the whole point of the texture work is that the install stays as Steam left it.

The @UTF tables are stored under CRI's standard byte mask, which is a fixed
keystream rather than a secret, so the reader unmasks and parses them directly
instead of shelling out to a tool that would have to be downloaded and trusted.
"""
import struct

_KEYSTREAM = bytearray()


def _keystream(length):
    global _KEYSTREAM
    if len(_KEYSTREAM) < length:
        multiplier, step = 0x0000655F, 0x00004115
        stream = bytearray(length)
        for i in range(length):
            stream[i] = multiplier & 0xFF
            multiplier = (multiplier * step) & 0xFFFFFFFF
        _KEYSTREAM = stream
    return _KEYSTREAM[:length]


def _unmask(data):
    key = _keystream(len(data))
    return (int.from_bytes(data, 'big') ^ int.from_bytes(key, 'big')).to_bytes(len(data), 'big')


_TYPES = {0: 'B', 1: 'b', 2: 'H', 3: 'h', 4: 'I', 5: 'i', 6: 'Q', 7: 'q', 8: 'f', 9: 'd',
          0xA: 'str', 0xB: 'data'}
_FORMATS = {'B': '>B', 'b': '>b', 'H': '>H', 'h': '>h', 'I': '>I', 'i': '>i',
            'Q': '>Q', 'q': '>q', 'f': '>f', 'd': '>d'}


def _read_value(body, position, kind, text):
    if kind == 'str':
        offset = struct.unpack_from('>I', body, position)[0]
        return text(offset), position + 4
    if kind == 'data':
        offset, length = struct.unpack_from('>II', body, position)
        return (offset, length), position + 8
    fmt = _FORMATS[kind]
    return struct.unpack_from(fmt, body, position)[0], position + struct.calcsize(fmt)


def parse_utf(block):
    """One @UTF table as (name, columns, row_count, row_iterator)."""
    if block[:4] != b'@UTF':
        block = _unmask(block)
    if block[:4] != b'@UTF':
        raise ValueError('not an @UTF table: %s' % block[:8].hex())
    body = block[8:8 + struct.unpack_from('>I', block, 4)[0]]
    rows_at, strings_at, data_at, name_at, columns, row_width, row_count = \
        struct.unpack_from('>IIIIHHI', body, 0)
    strings = body[strings_at:data_at]

    def text(offset):
        return strings[offset:strings.index(b'\0', offset)].decode('utf-8', 'replace')

    described = []
    position = 0x18
    for _ in range(columns):
        flags = body[position]
        position += 1
        storage, kind = flags & 0xF0, _TYPES[flags & 0x0F]
        name = text(struct.unpack_from('>I', body, position)[0])
        position += 4
        constant = None
        if storage == 0x30:
            constant, position = _read_value(body, position, kind, text)
        described.append((name, kind, storage, constant))

    def rows():
        for index in range(row_count):
            at = rows_at + index * row_width
            row = {}
            for name, kind, storage, constant in described:
                if storage == 0x30:
                    row[name] = constant
                elif storage == 0x10:
                    row[name] = 0
                else:
                    row[name], at = _read_value(body, at, kind, text)
            yield row

    return text(name_at), described, row_count, rows


def _read_block(path, offset):
    with open(path, 'rb') as handle:
        handle.seek(offset)
        head = handle.read(16)
        return head[:4], handle.read(struct.unpack_from('<Q', head, 8)[0])


class Cpk:
    """One archive, its table of contents read once."""

    def __init__(self, path):
        self.path = path
        _, header_block = _read_block(path, 0)
        self.header = next(parse_utf(header_block)[3]())
        toc_at = int(self.header['TocOffset'])
        # File offsets in the table are relative to the table's own position,
        # not to ContentOffset. Reading one known file both ways is what says so:
        # WEAPON/V_NULL.RAB lands on its 'SSA\0' magic only from TocOffset.
        self.base = toc_at
        _, toc_block = _read_block(path, toc_at)
        self.entries = list(parse_utf(toc_block)[3]())
        self.index = {(e['DirName'], e['FileName']): e for e in self.entries}

    def __len__(self):
        return len(self.entries)

    def names(self, directory=None, suffix=None):
        for entry in self.entries:
            if directory is not None and entry['DirName'] != directory:
                continue
            if suffix is not None and not entry['FileName'].upper().endswith(suffix.upper()):
                continue
            yield entry['DirName'], entry['FileName']

    def read(self, directory, name):
        entry = self.index[(directory, name)]
        with open(self.path, 'rb') as handle:
            handle.seek(self.base + int(entry['FileOffset']))
            data = handle.read(int(entry['FileSize']))
        if int(entry['ExtractSize']) != int(entry['FileSize']):
            raise NotImplementedError('%s/%s is CRILAYLA compressed' % (directory, name))
        return data
