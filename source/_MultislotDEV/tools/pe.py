"""Read-only PE inspection for the multislot research. All addresses are RVAs.

Usage examples:
  python -B tools/pe.py --info
  python -B tools/pe.py --import-calls EOS_Lobby_CreateLobby
  python -B tools/pe.py --function 0x123456
  python -B tools/pe.py --rip-refs 0x1a2b3c
  python -B tools/pe.py --file path/to/other.dll --disasm 0x1000 --size 200
"""
import argparse, bisect, hashlib, pathlib, re, struct, sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / 'python_libs'))
import pefile  # noqa: E402
from capstone import Cs, CS_ARCH_X86, CS_MODE_64  # noqa: E402
from capstone.x86 import X86_OP_MEM, X86_REG_RIP  # noqa: E402

GAME = HERE.parents[1]
# ModRM bytes with mod=00 rm=101: RIP-relative disp32 follows.
_RIP_MODRM = re.compile(b'[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]')


class Binary:
    def __init__(self, path=GAME / 'EDF.dll'):
        self.path = pathlib.Path(path)
        self.data = self.path.read_bytes()
        self.pe = pefile.PE(data=self.data, fast_load=True)
        self.pe.parse_data_directories(directories=[
            pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT'],
            pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT']])
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.md = Cs(CS_ARCH_X86, CS_MODE_64)
        self.md.detail = True
        exc = self.pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
        pdata = self.pe.get_data(exc.VirtualAddress, exc.Size)
        self.funcs = sorted((s, e) for s, e, _ in struct.iter_unpack('<III', pdata[:len(pdata) // 12 * 12]) if s and e > s)
        self.func_starts = [s for s, _ in self.funcs]
        self.imports = {}
        for entry in getattr(self.pe, 'DIRECTORY_ENTRY_IMPORT', []):
            for imp in entry.imports:
                if imp.name:
                    self.imports[imp.name.decode()] = imp.address - self.base

    def read(self, rva, size):
        return self.pe.get_data(rva, size)

    def q(self, rva):
        return struct.unpack('<Q', self.read(rva, 8))[0]

    def text_sections(self):
        return [s for s in self.pe.sections if s.Characteristics & 0x20000000]

    def containing(self, rva):
        i = bisect.bisect_right(self.func_starts, rva) - 1
        if i >= 0 and self.funcs[i][0] <= rva < self.funcs[i][1]:
            return self.funcs[i]
        return None

    def scan(self, sig):
        sig = sig.replace(' ', '')
        pat = b''.join(b'.' if sig[i:i + 2] == '??' else re.escape(bytes.fromhex(sig[i:i + 2])) for i in range(0, len(sig), 2))
        return [self.pe.get_rva_from_offset(m.start()) for m in re.finditer(pat, self.data, re.DOTALL)]

    def insns(self, rva, size):
        return list(self.md.disasm(self.read(rva, size), rva))

    def disasm(self, rva, size, out=print):
        for i in self.md.disasm(self.read(rva, size), rva):
            note = ''
            for o in i.operands:
                if o.type == X86_OP_MEM and o.mem.base == X86_REG_RIP:
                    note = f'  ; -> {i.address + i.size + o.mem.disp:08X}'
            out(f'{i.address:08X}  {i.bytes.hex(" "):38} {i.mnemonic:9} {i.op_str}{note}')

    def rip_refs(self, target):
        """Instructions whose RIP-relative memory operand resolves to target (RVA)."""
        by_end = {}
        for s in self.text_sections():
            data = s.get_data()
            base = s.VirtualAddress
            for m in _RIP_MODRM.finditer(data):
                pos = m.start()
                if pos + 5 > len(data):
                    continue
                disp = struct.unpack_from('<i', data, pos + 1)[0]
                end_min = base + pos + 5 + disp
                if not 0 <= target - end_min <= 4:
                    continue
                for start in range(max(0, pos - 4), pos):
                    for ins in self.md.disasm(data[start:start + 15], base + start, count=1):
                        if any(o.type == X86_OP_MEM and o.mem.base == X86_REG_RIP and
                               ins.address + ins.size + o.mem.disp == target for o in ins.operands):
                            end = ins.address + ins.size
                            # A decode starting one byte late (after a REX prefix) ends at the
                            # same place; keep the longest, which is the real instruction.
                            if end not in by_end or ins.address < by_end[end][0]:
                                by_end[end] = (ins.address, f'{ins.mnemonic} {ins.op_str}')
        return sorted(by_end.values())

    def calls_to(self, target):
        hits = []
        for s in self.text_sections():
            data = s.get_data()
            base = s.VirtualAddress
            for m in re.finditer(b'[\xe8\xe9]', data):
                pos = m.start()
                if pos + 5 <= len(data) and base + pos + 5 + struct.unpack_from('<i', data, pos + 1)[0] == target:
                    hits.append(base + pos)
        return hits

    def import_calls(self, name):
        slot = self.imports[name]
        hits = []
        for s in self.text_sections():
            data = s.get_data()
            for m in re.finditer(b'\xff[\x15\x25]', data):
                pos = m.start()
                if pos + 6 <= len(data) and s.VirtualAddress + pos + 6 + struct.unpack_from('<i', data, pos + 2)[0] == slot:
                    hits.append(s.VirtualAddress + pos)
        return slot, hits

    def pointers_to(self, target):
        pat = re.escape(struct.pack('<Q', self.base + target))
        return [self.pe.get_rva_from_offset(m.start()) for m in re.finditer(pat, self.data)]

    def string_rvas(self, needle, wide=False):
        enc = needle.encode('utf-16le') if wide else needle.encode()
        return [self.pe.get_rva_from_offset(m.start()) for m in re.finditer(re.escape(enc), self.data)]

    def rtti_vtables(self, type_name):
        """type_name like '.?AVHUiRoom@ui@@' -> list of (vtable_rva, this_offset)."""
        idx = self.data.find(type_name.encode() + b'\0')
        if idx < 0:
            return []
        td = self.pe.get_rva_from_offset(idx) - 16
        out = []
        for m in re.finditer(re.escape(struct.pack('<I', td)), self.data):
            off = m.start() - 12
            if off < 0:
                continue
            vals = struct.unpack_from('<6I', self.data, off)
            col = self.pe.get_rva_from_offset(off)
            if vals[0] != 1 or col is None or vals[5] != col:
                continue
            for v in re.finditer(re.escape(struct.pack('<Q', self.base + col)), self.data):
                out.append((self.pe.get_rva_from_offset(v.start() + 8), vals[1]))
        return out

    def vtable_entries(self, vt, limit=300):
        text = self.text_sections()[0]
        lo, hi = text.VirtualAddress, text.VirtualAddress + text.Misc_VirtualSize
        out = []
        for i in range(limit):
            p = self.q(vt + i * 8) - self.base
            if not lo <= p < hi:
                break
            out.append(p)
        return out


def fmt(f):
    return f'{f[0]:#x}-{f[1]:#x}' if f else None


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--file', default=str(GAME / 'EDF.dll'))
    p.add_argument('--info', action='store_true')
    p.add_argument('--scan')
    p.add_argument('--disasm')
    p.add_argument('--size', type=lambda x: int(x, 0), default=256)
    p.add_argument('--function')
    p.add_argument('--calls')
    p.add_argument('--import-calls')
    p.add_argument('--rip-refs')
    p.add_argument('--string')
    p.add_argument('--wide', action='store_true')
    p.add_argument('--rtti')
    args = p.parse_args()
    b = Binary(args.file)
    if args.info:
        print('FILE', b.path, 'SHA256', hashlib.sha256(b.data).hexdigest())
        print('ImageBase', hex(b.base), 'SizeOfImage', hex(b.pe.OPTIONAL_HEADER.SizeOfImage))
        for s in b.pe.sections:
            print(s.Name.decode().strip('\0'), hex(s.VirtualAddress), hex(s.Misc_VirtualSize))
    if args.scan:
        for a in b.scan(args.scan):
            print(hex(a), 'function', fmt(b.containing(a)))
    if args.disasm:
        b.disasm(int(args.disasm, 0), args.size)
    if args.function:
        f = b.containing(int(args.function, 0))
        print('FUNCTION', fmt(f))
        if f:
            b.disasm(f[0], f[1] - f[0])
    if args.calls:
        for a in b.calls_to(int(args.calls, 0)):
            print(hex(a), 'function', fmt(b.containing(a)))
    if args.import_calls:
        slot, hits = b.import_calls(args.import_calls)
        print('IAT slot', hex(slot))
        for a in hits:
            print(hex(a), 'function', fmt(b.containing(a)))
    if args.rip_refs:
        for a, text in b.rip_refs(int(args.rip_refs, 0)):
            print(hex(a), text, 'function', fmt(b.containing(a)))
    if args.string:
        for a in b.string_rvas(args.string, args.wide):
            print(hex(a))
    if args.rtti:
        for vt, off in b.rtti_vtables(args.rtti):
            print('vtable', hex(vt), 'offset', hex(off), 'entries', len(b.vtable_entries(vt)))
