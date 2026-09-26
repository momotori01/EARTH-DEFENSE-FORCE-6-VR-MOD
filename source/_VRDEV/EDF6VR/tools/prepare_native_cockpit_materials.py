"""Reuse owned EDF6 Nix paint/metal texels and their aligned BC5 normal map.

Read-only CPK extraction, atlas remapping and colour-space conversion. No AI
image generation or painted bolts/details. Original DDS hashes are recorded.
Run with the workspace Pillow/numpy Python before build_cockpit_blend.py.
"""
from pathlib import Path
import hashlib, io, json, re
import numpy as np
from PIL import Image
from edf6.cpk import Cpk
from edf6.rab import parse
from edf6.dds import read as dds_info

root=Path(__file__).resolve().parents[1]
out=root/'assets/cockpit/textures/native_v1'
tiles=out/'tiles';tiles.mkdir(parents=True,exist_ok=True)
archive='V612_NIX_BLACK.MRAB'
rab=parse(Cpk(root.parent.parent/'Root.cpk').read('OBJECT',archive))
names={'BaseColor':'v612_newnixblack_df.DDS','Normal':'v612_newnix_nm.DDS'}
maps={};records=[]
for kind,name in names.items():
    raw=rab.find(name).data
    records.append(dict(cpk='Root.cpk',archive=archive,file=name,sha256=hashlib.sha256(raw).hexdigest(),bytes=len(raw)))
    maps[kind]=Image.open(io.BytesIO(raw)).convert('RGB')
    if kind=='Normal':
        assert dds_info(raw).format=='BC5_UNORM', 'expected BC5_UNORM RG normal'

source=(root/'src/cockpit_geometry.cpp').read_text()
block=source.split('const P colours[]=')[1].split(';')[0]
colours=[np.array([float(v.strip().rstrip('f')) for v in g.split(',')]) for g in re.findall(r'\{([^{}]+)\}',block)]
def linear(s):return np.where(s<=.04045,s/12.92,((s+.055)/1.055)**2.4)
def srgb(s):return np.where(s<=.0031308,s*12.92,1.055*np.maximum(s,0)**(1/2.4)-.055)
def bitmap(a):return Image.fromarray(np.uint8(np.clip(a*255+.5,0,255)))

# Broad painted panels, free of drawn switches/bolts. Keep paired map UVs exact.
patches={'paint':(478,1050,525,1138),'trim':(638,1088,693,1147),'steel':(900,608,954,728)}
uses={0:'paint',1:'trim',2:'steel',4:'trim',5:'trim',6:'paint'}
atlases={k:Image.new('RGB',(2048,2048)) for k in ('BaseColor','Roughness','Emissive','Normal')}
for tile in range(16):
    colour=np.broadcast_to(colours[tile],(480,480,3)).copy()
    finish=.81 if tile in (3,12) else .27 if tile==2 else .35
    rough=np.full((480,480,3),finish)
    nm=np.zeros((480,480,3));nm[:,:,2]=1
    if tile in uses:
        box=patches[uses[tile]]
        src=np.array(maps['BaseColor'].crop(box).resize((480,480),Image.Resampling.LANCZOS),dtype=np.float32)/255
        src=linear(src);median=np.maximum(np.median(src,axis=(0,1)),.003)
        # Keep existing blue-grey colour, preserve the game's actual scuffs.
        ratio=np.clip(src/median,.50,2.6)
        colour=colours[tile]*(.15+.85*ratio)
        wear=np.clip(np.mean(ratio,axis=2)-1,0,1)
        rough-=wear[:,:,None]*.06
        rg=np.array(maps['Normal'].crop(box).resize((480,480),Image.Resampling.BILINEAR),dtype=np.float32)[:,:,:2]/255*2-1
        # Subtle physical scuff normals, rather than importing a baked bolt.
        rg*=.28;nm[:,:,:2]=rg;nm[:,:,2]=np.sqrt(np.maximum(0,1-np.sum(rg*rg,axis=2)))
    emit=np.broadcast_to(colours[tile] if tile in (8,9) else [0,0,0],(480,480,3))
    images={'BaseColor':bitmap(srgb(colour)),'Roughness':bitmap(rough),'Emissive':bitmap(srgb(emit)), 'Normal':bitmap(nm*.5+.5)}
    x=tile%4*512;y=tile//4*512
    for kind,im in images.items():
        # Edge extrusion protects lower mip levels without changing the swatch.
        padded=np.pad(np.array(im),((16,16),(16,16),(0,0)),mode='edge')
        atlases[kind].paste(Image.fromarray(padded),(x,y))
        if kind=='Normal':
            a=np.array(im);a[:,:,1]=255-a[:,:,1];im=Image.fromarray(a) # glTF/OpenGL Y convention
        im.save(tiles/f'{tile:02d}_{kind}.png')
for kind,im in atlases.items():im.save(out/f'NixInterior_{kind}.png')
(out/'provenance.json').write_text(json.dumps(dict(sources=records,patches=patches,material_tiles=uses,
    normal='DDS BC5 UNORM RG, positive Z reconstructed, XY strength .28; atlas DirectX Y, glTF tiles Y-flipped',
    roughness='Authored finish levels; native packed rgb channels are NOT assumed to mean roughness/metallic',
    conversion='Crop matching BaseColor/Normal rectangles; median-linear tint to existing palette; edge extrusion'),indent=2),encoding='utf-8')
print('Prepared native Nix colour/normal atlas and 64 glTF material swatches.')
