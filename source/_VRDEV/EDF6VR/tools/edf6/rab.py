"""The SSA container EDF6 keeps models and textures in, and the CMPL stream
inside it.

Layout, read out of a shipped archive and then proved by rebuilding every one of
them byte for byte (tools/edf6/selftest.py):

    0x00  "SSA\\0"
    0x04  version, 0x110 in every EDF6 file seen
    0x08  offset of the first block
    0x0C  largest stored block
    0x10  largest block once decompressed
    0x14  record count
    0x18  record table offset
    0x1C  sorted-name index offset
    0x20  directory count
    0x24  directory table offset
    ...   records, 32 bytes each
    ...   sorted-name index, 8 bytes each
    ...   directory table, 4 bytes each
    ...   UTF-16LE names, NUL terminated
    ...   blocks, contiguous, no padding

Every name offset in the file is relative to the field that holds it, which is
why the tables can be moved around freely as long as each is rewritten whole.

The two size fields at 0x0C and 0x10 are what a naive rebuild gets wrong: they
are the largest stored and largest decompressed block in the file, and the game
sizes a buffer from them. Growing a texture without growing these is how a
texture pack would corrupt memory rather than simply look wrong.

Two things here are copied rather than derived, because the shipped files
disagree about them and being right by luck is not being right:

  * the order names appear in. Some archives write the directory names first and
    some write an entry name before them, so there is no rule to follow. An
    archive that is being edited already has an order, and keeping it is both
    exact and free.
  * the sorted-name index. It is sorted, but by whose comparison is not knowable
    from the outside, and the game presumably searches it. Reusing the order
    that shipped cannot be wrong; re-deriving it could be.

Directories are TEXTURE, MODEL, HD-TEXTURE and sometimes EFFECT, and a record's
type field is an index into that list. A texture usually appears twice: full
size under HD-TEXTURE, and at a sixteenth of the width as ".lod.DDS" under
TEXTURE for the game's low texture setting.

CMPL is LZSS with a 4096-byte ring starting at 4078 and matches up to 18 bytes,
the same scheme as the compressor published in
_VRDEV/References/Earth-Defence-Force-Documentation-master/CMPLCompress.cpp.
"""
import struct

RING = 4096
RING_START = 4078
MAX_MATCH = 18
FLAG_ALL_LITERAL = 0xFF


def decompress(source, limit=None):
    """One CMPL body (everything after the tag and the size) back to plain bytes.

    `limit` stops once that many bytes exist, which is how a texture's DDS
    header is read without paying for the picture: classifying a map means
    looking at two thousand headers and rewriting a couple of hundred images.
    """
    ring = bytearray(RING)
    write = RING_START
    out = bytearray()
    at, end = 0, len(source)
    while at < end and (limit is None or len(out) < limit):
        flags = source[at]
        at += 1
        for bit in range(8):
            if at >= end:
                break
            if (flags >> bit) & 1:
                byte = source[at]
                at += 1
                out.append(byte)
                ring[write] = byte
                write = (write + 1) & 0xFFF
            else:
                if at + 1 >= end:
                    return bytes(out)
                packed = (source[at] << 8) | source[at + 1]
                at += 2
                start, length = packed >> 4, (packed & 0xF) + 3
                for step in range(length):
                    byte = ring[(start + step) & 0xFFF]
                    out.append(byte)
                    ring[write] = byte
                    write = (write + 1) & 0xFFF
    return bytes(out)


def compress_stored(source):
    """A valid CMPL body that copies rather than compresses.

    Eight literals behind a flag byte of all ones is what the format calls a run
    of uncompressed bytes, so this is a legal stream that a decoder for it reads
    correctly; it just spends an extra byte in nine. Searching for real matches
    is the published compressor's four-thousand-entry scan per byte, which is
    minutes per megabyte and tens of hours over a map, and the textures written
    here are block compressed already, so there is little left to find.

    The layout is completely regular -- flag, eight bytes, flag, eight bytes --
    so it is built by strided slice assignment rather than a loop, which is the
    difference between disk speed and an afternoon.
    """
    whole = len(source) // 8
    out = bytearray([FLAG_ALL_LITERAL]) * (whole * 9)
    view = memoryview(source)
    for lane in range(8):
        out[lane + 1::9] = view[lane:whole * 8:8]
    tail = bytes(view[whole * 8:])
    if tail:
        out.append((1 << len(tail)) - 1)
        out += tail
    return bytes(out)


def unwrap(block, limit=None):
    """A stored block to its contents. Blocks that are not CMPL come back as they are."""
    if block[:4] != b'CMPL':
        return block[:limit] if limit is not None else block
    size = struct.unpack_from('>I', block, 4)[0]
    body = decompress(block[8:], limit)
    if limit is None and len(body) < size:
        raise ValueError('CMPL block short: %d of %d' % (len(body), size))
    return body[:size]


def wrap(data):
    return b'CMPL' + struct.pack('>I', len(data)) + compress_stored(data)


class Entry:
    """One block. Decompressed only when something actually reads it.

    Most of an archive is never touched by a texture pass: the models, the
    normal maps, everything already large enough. Those blocks travel from the
    old file to the new one in the form they shipped in, so the expensive half
    of the work happens once per texture really being replaced rather than once
    per block in the file.
    """
    __slots__ = ('name', 'directory', 'flags', 'stored', '_data')

    def __init__(self, name, directory, flags, stored, data=None):
        self.name = name              # "s_missile..._df.DDS", as the archive spells it
        self.directory = directory    # TEXTURE / MODEL / HD-TEXTURE / EFFECT
        self.flags = flags            # the fourth record word, meaning unknown, preserved
        self.stored = stored          # the block exactly as it shipped, or None
        self._data = data

    @property
    def data(self):
        if self._data is None:
            self._data = unwrap(self.stored)
        return self._data

    def head(self, length=160):
        """The first bytes of the contents, without decompressing the rest."""
        if self._data is not None:
            return self._data[:length]
        return unwrap(self.stored, limit=length)

    @property
    def plain_size(self):
        """The decompressed length without decompressing: CMPL says so itself."""
        if self._data is not None:
            return len(self._data)
        if self.stored[:4] == b'CMPL':
            return struct.unpack_from('>I', self.stored, 4)[0]
        return len(self.stored)

    def replace(self, data):
        """New contents. Drops the shipped block, so it gets written afresh."""
        self._data = data
        self.stored = None

    def __repr__(self):
        return 'Entry(%r, %r, %d bytes)' % (self.directory, self.name, self.plain_size)


class Archive:
    """One .rab/.mrab, taken apart. Edit `entries[i].replace(...)`, then `to_bytes()`."""

    def __init__(self, version, directories, entries, name_order, sorted_order):
        self.version = version
        self.directories = directories
        self.entries = entries
        self.name_order = name_order      # strings in the order the file wrote them
        self.sorted_order = sorted_order  # record indices in the file's own sort

    def find(self, name):
        for entry in self.entries:
            if entry.name.upper() == name.upper():
                return entry
        return None

    def textures(self):
        return [e for e in self.entries if e.name.upper().endswith('.DDS')]

    def to_bytes(self):
        return build(self)


def _read_name(blob, at):
    end = at
    while blob[end:end + 2] != b'\0\0':
        end += 2
    return blob[at:end].decode('utf-16-le')


def heads(path, limit=160, start=0):
    """(directory, name, first bytes) of every entry, straight from the file.

    For deciding about an archive that is already built from its textures'
    headers alone: a map is gigabytes, and all of it would be read by parse.
    Everything but the contents sits before the data, so one read covers the
    table, and each entry costs a seek and a few kilobytes. `start` is where
    the archive begins in the file, for one stored inside a .cpk.
    """
    with open(path, 'rb') as handle:
        handle.seek(start)
        head = handle.read(40)
        if head[:4] != b'SSA\0':
            raise ValueError('not an SSA container: %s' % head[:4].hex())
        (_version, data_at, _largest_stored, _largest_plain, count,
         records_at, _sorted_at, directory_count, directories_at) = struct.unpack_from('<9I', head, 4)
        handle.seek(start)
        table = handle.read(data_at)
        directories = []
        for index in range(directory_count):
            field = directories_at + 4 * index
            directories.append(_read_name(table, field + struct.unpack_from('<I', table, field)[0]))
        for index in range(count):
            record = records_at + 32 * index
            name_at, size, kind, _flags = struct.unpack_from('<4I', table, record)
            offset = struct.unpack_from('<Q', table, record + 24)[0]
            handle.seek(start + offset)
            stored = handle.read(min(size, 8192))
            yield directories[kind], _read_name(table, record + name_at), unwrap(stored, limit)


def parse(blob):
    """One archive taken apart. Blocks stay compressed until something reads them."""
    if blob[:4] != b'SSA\0':
        raise ValueError('not an SSA container: %s' % blob[:4].hex())
    (version, _data_at, _largest_stored, _largest_plain, count,
     records_at, sorted_at, directory_count, directories_at) = struct.unpack_from('<9I', blob, 4)

    seen = {}   # name offset -> text, so the write order can be recovered exactly
    directories = []
    for index in range(directory_count):
        field = directories_at + 4 * index
        at = field + struct.unpack_from('<I', blob, field)[0]
        name = _read_name(blob, at)
        seen[at] = name
        directories.append(name)

    entries = []
    for index in range(count):
        record = records_at + 32 * index
        name_at, size, kind, flags = struct.unpack_from('<4I', blob, record)
        offset = struct.unpack_from('<Q', blob, record + 24)[0]
        at = record + name_at
        name = _read_name(blob, at)
        seen[at] = name
        entries.append(Entry(name, directories[kind], flags,
                             bytes(blob[offset:offset + size])))

    name_order = [seen[at] for at in sorted(seen)]
    sorted_order = [struct.unpack_from('<I', blob, sorted_at + 8 * slot + 4)[0]
                    for slot in range(count)]
    return Archive(version, directories, entries, name_order, sorted_order)


def build(archive):
    """The inverse of parse. An untouched archive comes back byte for byte."""
    entries = archive.entries
    count = len(entries)
    records_at = 40
    sorted_at = records_at + 32 * count
    directories_at = sorted_at + 8 * count
    names_at = directories_at + 4 * len(archive.directories)

    order = list(archive.name_order)
    for name in archive.directories:
        if name not in order:
            order.append(name)
    for entry in entries:
        if entry.name not in order:
            order.append(entry.name)

    names, offsets = bytearray(), {}
    for text in order:
        if text in offsets:
            continue
        offsets[text] = names_at + len(names)
        names.extend(text.encode('utf-16-le') + b'\0\0')

    data_at = names_at + len(names)
    blocks, placed, at = [], [], data_at
    largest_stored = largest_plain = 0
    for entry in entries:
        block = entry.stored if entry.stored is not None else wrap(entry.data)
        blocks.append(block)
        placed.append((at, len(block)))
        largest_stored = max(largest_stored, len(block))
        largest_plain = max(largest_plain, entry.plain_size)
        at += len(block)

    out = bytearray(data_at)
    struct.pack_into('<4s9I', out, 0, b'SSA\0', archive.version, data_at, largest_stored,
                     largest_plain, count, records_at, sorted_at, len(archive.directories),
                     directories_at)
    for index, entry in enumerate(entries):
        record = records_at + 32 * index
        offset, size = placed[index]
        struct.pack_into('<4I', out, record, offsets[entry.name] - record, size,
                         archive.directories.index(entry.directory), entry.flags)
        struct.pack_into('<Q', out, record + 16, 0)
        struct.pack_into('<Q', out, record + 24, offset)
    for slot, index in enumerate(archive.sorted_order):
        field = sorted_at + 8 * slot
        struct.pack_into('<II', out, field, offsets[entries[index].name] - field, index)
    for index, name in enumerate(archive.directories):
        field = directories_at + 4 * index
        struct.pack_into('<I', out, field, offsets[name] - field)
    out[names_at:data_at] = names
    for block in blocks:
        out += block
    return bytes(out)
