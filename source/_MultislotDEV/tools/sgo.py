"""Minimal SGO / DSGO reader, either byte order (SGO format per edf-tools SGO.cpp). Read-only.

  python -B tools/sgo.py CONFIG.SGO [name-regex]
Prints top-level named values; arrays are expanded recursively.

DSGO (mission lists, most EDF6 object SGOs): header {'DSGO', node table offset, node count, node size}, then
16-byte nodes {value 8 bytes, type 4, pad 4}: type 0 double, 1 UTF-16 string at node + value, 2 integer,
3 array or dictionary at node + value = {name table offset, name count, index list offset, count}; names are
{string offset from the entry, member position}.
"""
import re, struct, sys


def _utf16(buf, off, big=False):
    end = off
    while end + 1 < len(buf) and buf[end:end + 2] != b'\0\0':
        end += 2
    return buf[off:end].decode('utf-16be' if big else 'utf-16le', 'replace')


class Sgo:
    def __init__(self, path=None, data=None):
        self.buf = data if data is not None else open(path, 'rb').read()
        if self.buf[:4] == b'SGO\0':
            self.e = '<'
        elif self.buf[:4] == b'\0OGS':
            self.e = '>'
        else:
            raise SystemExit(f'{path}: not an SGO (magic {self.buf[:4]!r})')

    def u(self, fmt, off):
        return struct.unpack_from(self.e + fmt, self.buf, off)

    def utf16(self, off):
        return _utf16(self.buf, off, self.e == '>')

    def node(self, pos):
        typ, a, b = self.u('iiI', pos)
        if typ == 0:
            return [self.node(pos + b + i * 12) for i in range(a)]
        if typ == 1:
            return self.u('i', pos + 8)[0]
        if typ == 2:
            return round(self.u('f', pos + 8)[0], 6)
        if typ == 3:
            return self.utf16(pos + b) if a else ''
        if typ == 4:
            return 'raw:' + self.buf[pos + b:pos + b + a].hex()
        return f'?type{typ}'

    def values(self):
        count, data_off, name_count, name_off = self.u('4I', 8)
        names = {}
        for i in range(name_count):
            p = name_off + i * 8
            rel, idx = self.u('iI', p)
            names[idx] = self.utf16(p + rel)
        return {names.get(i, f'#{i}'): self.node(data_off + i * 12) for i in range(count)}


class Dsgo:
    def __init__(self, path=None, data=None):
        self.buf = data if data is not None else open(path, 'rb').read()
        if self.buf[:4] != b'DSGO':
            raise SystemExit(f'{path}: not a DSGO (magic {self.buf[:4]!r})')
        self.table, self.count, self.size = struct.unpack_from('<III', self.buf, 4)

    def node(self, i):
        pos = self.table + i * self.size
        raw, typ = struct.unpack_from('<QI', self.buf, pos)
        if typ == 0:
            return round(struct.unpack_from('<d', self.buf, pos)[0], 6)
        if typ == 1:
            return _utf16(self.buf, pos + raw)
        if typ == 2:
            return struct.unpack_from('<q', self.buf, pos)[0]
        if typ == 3:
            d = pos + raw
            name_off, name_count, idx_off, count = struct.unpack_from('<IIII', self.buf, d)
            items = [self.node(j) for j in struct.unpack_from(f'<{count}I', self.buf, d + idx_off)]
            if not name_count:
                return items
            names = {}
            for k in range(name_count):
                e = d + name_off + k * 8
                so, member = struct.unpack_from('<II', self.buf, e)
                names[member] = _utf16(self.buf, e + so)
            return {names.get(m, f'#{m}'): items[m] for m in range(count)}
        return f'?type{typ}:{raw:#x}'

    def values(self):
        return self.node(0)


def load(path=None, data=None):
    """Top-level value of an SGO or DSGO file (a dict of named values for both)."""
    buf = data if data is not None else open(path, 'rb').read()
    return (Dsgo if buf[:4] == b'DSGO' else Sgo)(path, buf).values()


if __name__ == '__main__':
    values = load(sys.argv[1])
    pat = re.compile(sys.argv[2], re.I) if len(sys.argv) > 2 else None
    for k, v in (values.items() if isinstance(values, dict) else enumerate(values)):
        if pat is None or pat.search(str(k)):
            print(k, '=', v)
