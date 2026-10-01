"""The crew figures' source data, generated from the player's own game files.

EDF6VR shows the other crew member of a tandem cockpit (the Proteus MK2's
driver and missile operator) as that player's own soldier, seated. Nothing of
the game is shipped: this reads the installed Root.cpk and writes, next to the
plugin (Mods/Plugins/EDF6VRCrew):

  <MODEL>.crew      one player body model (P501..P508, P601..P608: four looks a
                    class): its skeleton, its materials and its meshes, the
                    level-of-detail 0 mesh of the model's MDB
  <MODEL>_<n>.dds   the textures those materials use, their larger mips cut so
                    none is wider than 1024 (the figure is seen from a seat away)
  <CLASS>.pose      frame 0 of the class's `vehicle_striker_rear` clip -- the
                    Grape's rear seat, which every class can ride -- as local
                    bone transforms
  <CLASS>.colors    the class's colour presets (CUSTOMCOLOR<CLASS>.SGO): main
                    and sub colour pairs, which the models' 2ColorChange
                    material paints through its colour mask (param_cm0_cm1)

Formats (little endian) are read by src/crew_figures.cpp; the MDB and CANM
layouts follow the blender-mdb-addon's description of them (Smileynator et al.,
CC BY-NC 4.0; format knowledge only, no code of it is used here).
Standard library only, so the embedded Python of Mods/HDTexture runs it.

usage: python crew_figures.py [game folder] [output folder]
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cpk      # noqa: E402
import dds      # noqa: E402
import rab      # noqa: E402

VERSION = 2   # 2: the roughness map from any param_r_m_occ_* slot (_light, _hr, _xxx)
MODELS = {
    'RANGER': ['P605_RANGER', 'P505_RANGER', 'P601_PROTO_RANGER', 'P501_PROTO_RANGER'],
    'WINGDIVER': ['P606_WINGDIVER', 'P506_WINGDIVER', 'P602_PROTO_WINGDIVER', 'P502_PROTO_WINGDIVER'],
    'AIRRAIDER': ['P608_AIRRADER', 'P508_AIRRADER', 'P604_PROTO_AIRRADER', 'P504_PROTO_AIRRADER'],
    'FENCER': ['P607_FENCER', 'P507_FENCER', 'P603_PROTO_FENCER', 'P503_PROTO_FENCER'],
}
ANIMATIONS = {'RANGER': 'EDF6ARMYSOLDIER.CAS', 'WINGDIVER': 'EDF6_WINGDIVER.CAS',
              'AIRRAIDER': 'EDF6ENGINEER.CAS', 'FENCER': 'EDF6HEAVYARMOR.CAS'}
COLOURS = {'RANGER': 'CUSTOMCOLORRANGER.SGO', 'WINGDIVER': 'CUSTOMCOLORWINGDIVER.SGO',
           'AIRRAIDER': 'CUSTOMCOLORAIRRAIDER.SGO', 'FENCER': 'CUSTOMCOLORHEAVYARMOR.SGO'}
SEATED_CLIP = 'vehicle_striker_rear'
MAX_TEXTURE = 1024


# --- CRILAYLA, the CPK's own compression (some OBJECT files use it) ---------
def crilayla(data):
    if not data.startswith(b'CRILAYLA'):
        return data
    size, header_at = struct.unpack_from('<II', data, 8)
    out = bytearray(size + 0x100)
    out[:0x100] = data[header_at + 0x10:header_at + 0x110]
    state = {'at': len(data) - 0x101, 'pool': 0, 'left': 0}

    def bits(count):
        value = 0
        while count:
            if not state['left']:
                state['pool'] = data[state['at']]
                state['at'] -= 1
                state['left'] = 8
            take = min(count, state['left'])
            value = (value << take) | ((state['pool'] >> (state['left'] - take)) & ((1 << take) - 1))
            count -= take
            state['left'] -= take
        return value
    write = len(out) - 1
    while write >= 0x100:
        if not bits(1):
            out[write] = bits(8)
            write -= 1
            continue
        source = write + bits(13) + 3
        count = 3
        for width in (2, 3, 5, 8):
            extra = bits(width)
            count += extra
            if extra != (1 << width) - 1:
                break
        else:
            while True:
                extra = bits(8)
                count += extra
                if extra != 255:
                    break
        for _ in range(count):
            out[write] = out[source]
            write -= 1
            source -= 1
    return bytes(out)


def read_file(archive, directory, name):
    entry = archive.index[(directory, name)]
    with open(archive.path, 'rb') as handle:
        handle.seek(archive.base + int(entry['FileOffset']))
        data = handle.read(int(entry['FileSize']))
    return crilayla(data) if int(entry['ExtractSize']) != int(entry['FileSize']) else data


def wide(blob, at, encoding='utf-16-le'):
    end = at
    while blob[end:end + 2] != b'\0\0':
        end += 2
    return blob[at:end].decode(encoding)


def narrow(blob, at):
    end = blob.index(b'\0', at)
    return blob[at:end].decode('ascii', 'replace')


# --- MDB: skeleton, materials, textures, LOD 0 meshes ---------------------
def parse_mdb(blob):
    if blob[:4] != b'MDB0':
        raise ValueError('not an MDB')
    (_version, name_count, name_at, bone_count, bone_at, object_count, object_at,
     material_count, material_at, texture_count, texture_at) = struct.unpack_from('<11I', blob, 4)
    names = []
    for i in range(name_count):
        record = name_at + 4 * i
        offset = struct.unpack_from('<I', blob, record)[0]
        names.append(wide(blob, record + offset) if offset else '')
    bones = []
    for i in range(bone_count):
        record = bone_at + 0xC0 * i
        _index, parent = struct.unpack_from('<Ii', blob, record)
        name = names[struct.unpack_from('<I', blob, record + 16)[0]]
        local = struct.unpack_from('<16f', blob, record + 32)
        inverse = struct.unpack_from('<16f', blob, record + 96)
        bones.append((name, parent, local, inverse))
    textures = []
    for i in range(texture_count):
        record = texture_at + 0x10 * i
        _index, name_offset, file_offset = struct.unpack_from('<III', blob, record)
        textures.append(wide(blob, record + file_offset))
    materials = []
    for i in range(material_count):
        record = material_at + 0x20 * i
        name_index, shader_at, param_at, param_count, texture_at_, texture_count_ = struct.unpack_from('<6I', blob, record + 4)
        shader = wide(blob, record + shader_at)
        params = {}
        for k in range(param_count):
            p = record + param_at + 0x20 * k
            values = struct.unpack_from('<6f', blob, p)
            label = narrow(blob, p + struct.unpack_from('<I', blob, p + 24)[0])
            size = blob[p + 29]
            params[label] = values[:max(1, min(size, 4))]
        maps = {}
        for k in range(texture_count_):
            t = record + texture_at_ + 0x1C * k
            texture_index, type_at = struct.unpack_from('<II', blob, t)
            maps[narrow(blob, t + type_at)] = textures[texture_index]
        materials.append({'name': names[name_index], 'shader': shader, 'params': params, 'maps': maps})
    meshes = []
    for i in range(object_count):
        record = object_at + 0x10 * i
        _index, _name, mesh_count, mesh_at = struct.unpack_from('<4I', blob, record)
        for k in range(mesh_count):
            m = record + mesh_at + 0x28 * k
            material = struct.unpack_from('<i', blob, m + 4)[0]
            layout_at, stride, layout_count, vertex_count, _mesh_index, vertex_at, index_count, index_at = \
                struct.unpack_from('<IHHIIIII', blob, m + 12)
            layout = {}
            for e in range(layout_count):
                l = m + layout_at + 0x10 * e
                kind, offset, channel, label_at = struct.unpack_from('<4I', blob, l)
                layout[(narrow(blob, l + label_at).upper(), channel)] = (kind, offset)
            meshes.append({'material': material, 'stride': stride, 'count': vertex_count,
                           'vertices': blob[m + vertex_at:m + vertex_at + stride * vertex_count],
                           'layout': layout,
                           'indices': struct.unpack_from('<%dH' % index_count, blob, m + index_at)})
    return bones, materials, meshes


def element(vertex, layout, name, width):
    kind, offset = layout[(name, 0)]
    if kind == 7:
        return struct.unpack_from('<4e', vertex, offset)[:width]
    if kind == 1:
        return struct.unpack_from('<4f', vertex, offset)[:width]
    if kind == 4:
        return struct.unpack_from('<3f', vertex, offset)[:width]
    if kind == 12:
        return struct.unpack_from('<2f', vertex, offset)[:width]
    if kind == 21:
        return struct.unpack_from('<4B', vertex, offset)[:width]
    raise ValueError('vertex element type %d' % kind)


def fixed(text, size):
    raw = text.encode('utf-8')[:size - 1]
    return raw + b'\0' * (size - len(raw))


def trimmed_dds(blob):
    """The same texture, its mips larger than MAX_TEXTURE dropped (BC blocks)."""
    info = dds.read(blob)
    if not info.ok:
        return blob
    width, height, mips = info.width, info.height, max(1, info.mips)
    header = 148 if info.dx10 else 128
    block = 8 if info.format.startswith(('BC1', 'BC4')) else 16
    compressed = info.format.startswith('BC')
    if not compressed:
        return blob
    level_sizes = []
    w, h = width, height
    for _ in range(mips):
        level_sizes.append(max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * block)
        w, h = max(1, w // 2), max(1, h // 2)
    drop = 0
    w, h = width, height
    while drop < mips - 1 and max(w, h) > MAX_TEXTURE:
        w, h = max(1, w // 2), max(1, h // 2)
        drop += 1
    if not drop:
        return blob
    start = header + sum(level_sizes[:drop])
    out = bytearray(blob[:header])
    struct.pack_into('<II', out, 12, h, w)
    struct.pack_into('<I', out, 28, mips - drop)
    struct.pack_into('<I', out, 20, level_sizes[drop])      # pitch or linear size of the new top level
    return bytes(out) + blob[start:start + sum(level_sizes[drop:])]


def write_model(archive, model, out_dir):
    blob = read_file(archive, 'OBJECT', model + '.MRAB')
    arc = rab.parse(blob)
    stem = model.lower().replace('_proto_', '_proto_')
    mdb_entry = None
    for entry in arc.entries:
        low = entry.name.lower()
        if entry.directory == 'MODEL' and low.endswith('.mdb') and '-lod' not in low:
            mdb_entry = entry
    if mdb_entry is None:
        raise ValueError('%s: no LOD 0 MDB' % model)
    bones, materials, meshes = parse_mdb(mdb_entry.data)
    # Textures: the HD copy when the archive has one, the .lod one otherwise.
    files = {}
    for material in materials:
        for texture in material['maps'].values():
            if texture in files:
                continue
            base = texture[:-4] if texture.lower().endswith('.dds') else texture
            source = None
            for entry in arc.entries:
                if entry.directory == 'HD-TEXTURE' and entry.name.lower() == texture.lower():
                    source = entry
            if source is None:
                for entry in arc.entries:
                    if entry.name.lower() in (texture.lower(), (base + '.lod.dds').lower()):
                        source = entry
            if source is None:
                files[texture] = ''
                continue
            name = '%s_%d.dds' % (model, len(files))
            with open(os.path.join(out_dir, name), 'wb') as handle:
                handle.write(trimmed_dds(source.data))
            files[texture] = name
    out = bytearray(b'CRW1')
    out += struct.pack('<4I', VERSION, len(bones), len(materials), len(meshes))
    for name, parent, local, inverse in bones:
        out += fixed(name, 32) + struct.pack('<i', parent) + struct.pack('<16f', *local) + struct.pack('<16f', *inverse)
    for material in materials:
        maps = material['maps']
        c0 = (tuple(material['params'].get('change_color0', ())) + (1, 1, 1, 1))[:4]
        c1 = (tuple(material['params'].get('change_color1', ())) + (1, 1, 1, 1))[:4]
        out += fixed(material['name'], 64) + fixed(material['shader'], 64)
        # The roughness/metal/occlusion map is named by its fourth channel:
        # _light (2ColorChange_Light), _hr (2ColorChange), _xxx (hair).
        param = next((maps[k] for k in sorted(maps) if k.startswith('param_r_m_occ')), '')
        for texture in (maps.get('albedo', ''), maps.get('normal', ''), param, maps.get('param_cm0_cm1', '')):
            out += fixed(files.get(texture, ''), 64)
        out += struct.pack('<8f', *(c0 + c1))
    for mesh in meshes:
        layout, stride, raw = mesh['layout'], mesh['stride'], mesh['vertices']
        out += struct.pack('<iII', mesh['material'], mesh['count'], len(mesh['indices']))
        skinned = ('BLENDINDICES', 0) in layout
        for v in range(mesh['count']):
            vertex = raw[v * stride:(v + 1) * stride]
            position = element(vertex, layout, 'POSITION', 3)
            normal = element(vertex, layout, 'NORMAL', 3)
            uv = element(vertex, layout, 'TEXCOORD', 2)
            if skinned:
                index = element(vertex, layout, 'BLENDINDICES', 4)
                weight = element(vertex, layout, 'BLENDWEIGHT', 4)
            else:
                index, weight = (0, 0, 0, 0), (1, 0, 0, 0)
            out += struct.pack('<8f4B4f', *position, *normal, *uv, *index, *weight)
        out += struct.pack('<%dI' % len(mesh['indices']), *mesh['indices'])
    with open(os.path.join(out_dir, model + '.crew'), 'wb') as handle:
        handle.write(out)
    return len(bones), len(meshes), sum(m['count'] for m in meshes)


# --- CANM: frame 0 of the seated clip --------------------------------------
def quaternion_rows(x, y, z, w):
    """The rotation as row-vector rows (the game's convention: v' = v * M)."""
    column = ((1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)),
              (2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)),
              (2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)))
    return tuple(tuple(column[k][r] for k in range(3)) for r in range(3))


def euler_rows(x, y, z):
    import math
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    rx = ((1, 0, 0), (0, cx, -sx), (0, sx, cx))
    ry = ((cy, 0, sy), (0, 1, 0), (-sy, 0, cy))
    rz = ((cz, -sz, 0), (sz, cz, 0), (0, 0, 1))

    def mul(a, b):
        return tuple(tuple(sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)) for i in range(3))
    column = mul(mul(rz, ry), rx)
    return tuple(tuple(column[k][r] for k in range(3)) for r in range(3))


def seated_pose(blob, clip):
    at = blob.find(b'CANM')
    if at < 0:
        raise ValueError('no CANM in the animation set')
    canm = blob[at:]
    (_version, anim_count, anim_at, point_count, point_at, bone_count, bone_at) = struct.unpack_from('<7I', canm, 4)
    bone_names = []
    for i in range(bone_count):
        record = bone_at + 4 * i
        bone_names.append(wide(canm, record + struct.unpack_from('<i', canm, record)[0]))

    def point(index):
        record = point_at + 0x30 * index
        base = struct.unpack_from('<4f', canm, record)
        speed = struct.unpack_from('<4f', canm, record + 16)
        key_at, kind, key_count = struct.unpack_from('<iii', canm, record + 32)
        first = None
        if key_count > 1:
            if kind == 1:
                first = struct.unpack_from('<3H', canm, record + key_at)
            elif kind == 3:
                first = struct.unpack_from('<4f', canm, record + key_at)
        return base, speed, kind, first
    for i in range(anim_count):
        record = anim_at + 0x1C * i
        _loop, name_at, _duration, _frame, _keys, data_count, data_at = struct.unpack_from('<IifffII', canm, record)
        if wide(canm, record + name_at) != clip:
            continue
        pose = []
        for k in range(data_count):
            bone, t, r, s = struct.unpack_from('<Hhhh', canm, record + data_at + 8 * k)
            entry = {'name': bone_names[bone]}
            if t >= 0:
                base, speed, _kind, first = point(t)
                entry['t'] = tuple(base[j] + (first[j] * speed[j] if first else 0) for j in range(3))
            if r >= 0:
                base, speed, kind, first = point(r)
                if kind in (2, 3):
                    q = first if (kind == 3 and first) else base
                    entry['r'] = quaternion_rows(*q)
                else:
                    angles = tuple(base[j] + (first[j] * speed[j] if first else 0) for j in range(3))
                    entry['r'] = euler_rows(*angles)
            if s >= 0:
                base, speed, _kind, first = point(s)
                entry['s'] = tuple(base[j] + (first[j] * speed[j] if first else 0) for j in range(3))
            pose.append(entry)
        return pose
    raise ValueError('clip %s not found' % clip)


def write_pose(archive, family, out_dir):
    pose = seated_pose(read_file(archive, 'OBJECT', ANIMATIONS[family]), SEATED_CLIP)
    out = bytearray(b'CRP1') + struct.pack('<2I', VERSION, len(pose))
    for entry in pose:
        flags = (1 if 't' in entry else 0) | (2 if 'r' in entry else 0) | (4 if 's' in entry else 0)
        t = entry.get('t', (0, 0, 0))
        r = entry.get('r', ((1, 0, 0), (0, 1, 0), (0, 0, 1)))
        s = entry.get('s', (1, 1, 1))
        out += fixed(entry['name'], 32) + struct.pack('<I', flags) + struct.pack('<3f', *t)
        out += struct.pack('<9f', *(r[0] + r[1] + r[2])) + struct.pack('<3f', *s)
    with open(os.path.join(out_dir, family + '.pose'), 'wb') as handle:
        handle.write(out)
    return len(pose)


# --- CUSTOMCOLOR: the presets, big-endian SGO ------------------------------
def colour_presets(blob):
    order = '>' if blob[:4] == b'\0OGS' else '<'
    u32 = lambda at: struct.unpack_from(order + 'I', blob, at)[0]
    s32 = lambda at: struct.unpack_from(order + 'i', blob, at)[0]
    floats = []

    def walk(at):
        kind, count = u32(at), u32(at + 4)
        if kind == 0:
            target = at + s32(at + 8)
            for i in range(count):
                walk(target + 12 * i)
        elif kind == 2:
            floats.append(struct.unpack_from(order + 'f', blob, at + 8)[0])
    for i in range(u32(8)):
        walk(u32(12) + 12 * i)
    colours = [tuple(floats[i:i + 4]) for i in range(0, len(floats) - 3, 4)]
    return [(colours[i], colours[i + 1]) for i in range(0, len(colours) - 1, 2)]


def write_colours(archive, family, out_dir):
    presets = colour_presets(read_file(archive, 'DEFAULTPACKAGE', COLOURS[family]))
    out = bytearray(b'CRC1') + struct.pack('<2I', VERSION, len(presets))
    for main, sub in presets:
        out += struct.pack('<8f', *(main + sub))
    with open(os.path.join(out_dir, family + '.colors'), 'wb') as handle:
        handle.write(out)
    return len(presets)


def main():
    game = sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(os.path.join(HERE, '..', '..', '..', '..'))
    out_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(game, 'Mods', 'Plugins', 'EDF6VRCrew')
    root = os.path.join(game, 'Root.cpk')
    if not os.path.isfile(root):
        print('Root.cpk not found in', game)
        return 1
    os.makedirs(out_dir, exist_ok=True)
    archive = cpk.Cpk(root)
    # Progress as the settings window reads it: "[crew] NN% what".
    steps = sum(1 + len(models) for models in MODELS.values())
    done = 0

    def progress(what):
        print('[crew] %d%% %s' % (done * 100 // steps, what), flush=True)
    # Older figures go first: a run stopped part way must not leave them looking whole.
    stale = os.path.join(out_dir, 'version.txt')
    if os.path.isfile(stale):
        os.remove(stale)
    for family, models in MODELS.items():
        progress(family.capitalize())
        print(family, 'pose bones', write_pose(archive, family, out_dir),
              'colour presets', write_colours(archive, family, out_dir), flush=True)
        done += 1
        for model in models:
            progress(model)
            bones, meshes, vertices = write_model(archive, model, out_dir)
            print('  %s: %d bones, %d meshes, %d vertices' % (model, bones, meshes, vertices), flush=True)
            done += 1
    progress('done')
    with open(os.path.join(out_dir, 'version.txt'), 'w') as handle:
        handle.write('%d\n' % VERSION)
    print('Crew figures written to', out_dir)
    return 0


if __name__ == '__main__':
    sys.exit(main())
