"""The cockpit's material atlas: authored colours, with the grain and relief of
CC0 textures chosen by the user.

No game file is read. The painted panels (tiles 0, 6) take ambientCG's
PaintedMetal002, the metal (tile 2) and the dark trim (1, 4, 5) its Scratches004
(assets/cockpit/textures/cc0, CC0 1.0; the user picked them on 2026-09-29 from a
sheet of candidates, the panels' MetalPlates001 first, then Scratches001, then
PaintedMetal002 -- its brightness pattern only, not its blue). Each tile is the whole seamless texture brought down to
480 px (so it repeats without a seam; the shader lays a tile over 52 cm):
- colour: the tile's authored colour times .15 + .85 x the texture's brightness
  ratio (luminance over its median, only: the cockpit keeps its own hues),
  scaled to the material's strength, and kept at the old tiles' mean ratio (the
  painted cabins' shader divides by the tile's mean);
- normal: the texture's DirectX normal map (the atlas is DirectX Y), its
  slope scaled per material;
- roughness: the authored finish, a little smoother where the grain is lighter.
Until 2026-09-21..29 the grain was cropped from EDF6's own Nix textures, then
made from noise. Run with a Python that has Pillow and numpy.
"""
from pathlib import Path
import json, re
import numpy as np
from PIL import Image

root = Path(__file__).resolve().parents[1]
out = root / 'assets/cockpit/textures/native_v1'
tiles = out / 'tiles'; tiles.mkdir(parents=True, exist_ok=True)
cc0 = root / 'assets/cockpit/textures/cc0'

source = (root / 'src/cockpit_geometry.cpp').read_text()
block = source.split('const P colours[]=')[1].split(';')[0]
colours = [np.array([float(v.strip().rstrip('f')) for v in g.split(',')]) for g in re.findall(r'\{([^{}]+)\}', block)]
def linear(s): return np.where(s <= .04045, s / 12.92, ((s + .055) / 1.055) ** 2.4)
def srgb(s): return np.where(s <= .0031308, s * 12.92, 1.055 * np.maximum(s, 0) ** (1 / 2.4) - .055)
def bitmap(a): return Image.fromarray(np.uint8(np.clip(a * 255 + .5, 0, 255)))

N = 480
# Per material: its texture, the brightness ratio's spread (the old tiles'), the
# mean ratio the old tiles had (luminance weighted), and the normal's slope scale.
MATERIALS = {
    'paint': dict(texture='PaintedMetal002', spread=.089, mean=1.015, relief=1.0),
    'steel': dict(texture='Scratches004', spread=.073, mean=.983, relief=.7),
    'trim': dict(texture='Scratches004', spread=.060, mean=1.018, relief=.5),
}
uses = {0: 'paint', 6: 'paint', 2: 'steel', 1: 'trim', 4: 'trim', 5: 'trim'}


def load(name, kind):
    im = Image.open(cc0 / f'{name}_1K-JPG_{kind}.jpg').convert('RGB')
    return np.array(im.resize((N, N), Image.Resampling.LANCZOS), dtype=np.float64) / 255


def material(m):
    colour = linear(load(m['texture'], 'Color')) @ np.array([.2126, .7152, .0722])
    ratio = colour / max(np.median(colour), 1e-4)
    ratio = 1 + (ratio - 1) * (m['spread'] / max(ratio.std(), 1e-6))
    ratio *= m['mean'] / ratio.mean()
    n = load(m['texture'], 'NormalDX') * 2 - 1
    xy = n[:, :, :2] * m['relief']
    z = np.sqrt(np.maximum(0, 1 - np.sum(xy * xy, axis=2)))
    normal = np.dstack([xy, z])
    return np.clip(ratio, .5, 2.6), normal


built = {name: material(m) for name, m in MATERIALS.items()}
atlases = {k: Image.new('RGB', (2048, 2048)) for k in ('BaseColor', 'Roughness', 'Emissive', 'Normal')}
for tile in range(16):
    colour = np.broadcast_to(colours[tile], (N, N, 3)).copy()
    finish = .81 if tile in (3, 12) else .27 if tile == 2 else .35
    rough = np.full((N, N, 3), finish)
    nm = np.zeros((N, N, 3)); nm[:, :, 2] = 1
    if tile in uses:
        r, nm = built[uses[tile]]
        colour = colours[tile] * (.15 + .85 * r[:, :, None])
        wear = np.clip(r - 1, 0, 1)
        rough -= wear[:, :, None] * .06
    emit = np.broadcast_to(colours[tile] if tile in (8, 9) else [0, 0, 0], (N, N, 3))
    images = {'BaseColor': bitmap(srgb(colour)), 'Roughness': bitmap(rough), 'Emissive': bitmap(srgb(emit)), 'Normal': bitmap(nm * .5 + .5)}
    x = tile % 4 * 512; y = tile // 4 * 512
    for kind, im in images.items():
        # Wrapped padding: the tile repeats, so its border continues it (mip filtering).
        padded = np.pad(np.array(im), ((16, 16), (16, 16), (0, 0)), mode='wrap')
        atlases[kind].paste(Image.fromarray(padded), (x, y))
        if kind == 'Normal':
            a = np.array(im); a[:, :, 1] = 255 - a[:, :, 1]; im = Image.fromarray(a)   # glTF/OpenGL Y convention
        im.save(tiles / f'{tile:02d}_{kind}.png')
for kind, im in atlases.items(): im.save(out / f'NixInterior_{kind}.png')
(out / 'provenance.json').write_text(json.dumps(dict(
    sources=[dict(site='ambientCG', license='CC0 1.0', asset=a, maps=['Color', 'NormalDX'], resolution='1K-JPG')
             for a in sorted({m['texture'] for m in MATERIALS.values()})],
    materials=MATERIALS, material_tiles=uses,
    method='Whole seamless texture resized to 480 px per tile (the shader repeats a tile every 52 cm); colour: luminance ratio '
           'about the median, scaled to the material spread and mean, times the authored colour; normal: DirectX map, slope scaled. '
           "No game file is read or embedded (until 2026-09-21..29 the grain was cropped from EDF6's V612_NIX_BLACK textures).",
    roughness='Authored finish levels, less .06 x where the grain is lighter than its median'), indent=2), encoding='utf-8')
print('Prepared the cockpit atlases from', ', '.join(sorted({m['texture'] for m in MATERIALS.values()})))
