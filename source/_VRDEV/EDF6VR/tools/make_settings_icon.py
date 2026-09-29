"""The icon of "EDF6 VR setting.exe": the letters VR in red, nothing else.

    python tools/make_settings_icon.py

Writes assets/settings/vr.ico (16 to 256 px) and a 256 px PNG to look at.
Each size is drawn on its own rather than scaled down from the largest, so the
small ones stay crisp. Needs Pillow and Windows' Arial Black.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets' / 'settings'
FONT = r'C:\Windows\Fonts\ariblk.ttf'
RED = (220, 20, 30, 255)
SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)


def draw(size):
    # Big enough that "VR" fills the width; drawn 4x and reduced for smooth edges.
    scale = 4
    canvas = size * scale
    image = Image.new('RGBA', (canvas, canvas), (0, 0, 0, 0))
    pen = ImageDraw.Draw(image)
    points = canvas
    while points > 4:
        font = ImageFont.truetype(FONT, points)
        left, top, right, bottom = pen.textbbox((0, 0), 'VR', font=font)
        if right - left <= canvas * 0.96 and bottom - top <= canvas * 0.9:
            break
        points -= 2
    x = (canvas - (right - left)) / 2 - left
    y = (canvas - (bottom - top)) / 2 - top
    pen.text((x, y), 'VR', font=font, fill=RED)
    return image.resize((size, size), Image.LANCZOS)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    images = [draw(size) for size in SIZES]
    images[-1].save(OUT / 'vr.ico', format='ICO', sizes=[(s, s) for s in SIZES],
                    append_images=images[:-1])
    images[-1].save(OUT / 'vr_256.png')
    print('wrote', OUT / 'vr.ico')


if __name__ == '__main__':
    main()
