"""Build a race-free copy of the exact EDFModLoader winmm shipped with MultiSlot.

No DLL is loaded/executed. Only the 180 export thunks (19 bytes each) change.
Keep the original load of the per-export function pointer, then jump via rax;
never publish that pointer through the shared PA global. Unknown inputs fail.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from pe import Binary

ORIGINAL_SHA256 = 'B80E4DA6AE7264F0E9774C992DE3C5F9B6E9BC9422146ADEEB5BB2BD681E112E'


def transform(source):
    binary = Binary(source)
    original = binary.data
    if hashlib.sha256(original).hexdigest().upper() != ORIGINAL_SHA256:
        raise ValueError('Only the pinned original EDFModLoader is accepted')
    exports = binary.pe.DIRECTORY_ENTRY_EXPORT.symbols
    if len(exports) != 180 or binary.pe.FILE_HEADER.Machine != 0x8664:
        raise ValueError('Unexpected export count or architecture')
    fixed = bytearray(original)
    records = []
    slots = set()
    spans = set()
    for export in exports:
        rva = export.address
        code = binary.read(rva, 19)
        if export.forwarder or not export.name or code[:3] != b'\x48\x8b\x05' or code[7:10] != b'\x48\x89\x05' or code[14] != 0xe9:
            raise ValueError(f'Unexpected thunk at {rva:X}')
        slot = rva + 7 + struct.unpack_from('<i', code, 3)[0]
        shared = rva + 14 + struct.unpack_from('<i', code, 10)[0]
        gateway = rva + 19 + struct.unpack_from('<i', code, 15)[0]
        if shared != 0x1DF98 or gateway != 0xE7A0 or binary.read(gateway, 6) != bytes.fromhex('FF25F2F70000'):
            raise ValueError('Unexpected common dispatch')
        if slot % 8 or not 0x1E1A8 <= slot <= 0x1E740 or slot in slots:
            raise ValueError(f'Unexpected/duplicate target slot {slot:X}')
        slots.add(slot)
        offset = binary.pe.get_offset_from_rva(rva)
        span = set(range(offset, offset + 19))
        if spans & span:
            raise ValueError('Overlapping thunks')
        spans.update(span)
        replacement = code[:7] + b'\xff\xe0' + b'\x90' * 10
        fixed[offset:offset + 19] = replacement
        records.append(dict(name=export.name.decode('ascii'), ordinal=export.ordinal,
                            rva=rva, slot=slot, offset=offset, before=code.hex(), after=replacement.hex()))
    if len(original) != len(fixed) or any(a != b and i not in spans for i, (a, b) in enumerate(zip(original, fixed))):
        raise ValueError('Unexpected change outside the export thunks')
    return bytes(fixed), dict(original_sha256=ORIGINAL_SHA256,
                             fixed_sha256=hashlib.sha256(fixed).hexdigest().upper(),
                             changed_bytes=sum(a != b for a, b in zip(original, fixed)), exports=records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--manifest', type=Path, required=True)
    args = parser.parse_args()
    if args.source.resolve() in (args.output.resolve(), args.manifest.resolve()):
        raise ValueError('Never overwrite the original input')
    fixed, manifest = transform(args.source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(fixed)
    args.manifest.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(f"180 thunks fixed; {manifest['changed_bytes']} bytes changed; SHA256 {manifest['fixed_sha256']}")


if __name__ == '__main__':
    main()
