#!/usr/bin/env python3
"""Put your own picture on My Badge's picture tile (Streaming theme).

usage: python3 tools/set_picture.py <photo.jpg|png> [--circle]
       python3 tools/set_picture.py --default

Writes main/assets/img_me_card.c (86x86, the card view) and
main/assets/img_me_split.c (144x190, the split view), then build and flash
as usual. The photo is cropped from its middle to fill each tile.

--circle   the photo is a round avatar on a plain background: crop inside
           the circle, so none of the background shows
--default  go back to the drawn face the firmware ships with

Needs Pillow (pip install pillow).
"""
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TILES = (("img_me_card", 86, 86, 7), ("img_me_split", 144, 190, 9))


def crop(im, w, h, circle):
    W, H = im.size
    if circle:
        # The largest w:h box inside the avatar's circle, a little short of its edge.
        k = min(W, H) * 0.95 / (w * w + h * h) ** 0.5
    else:
        k = min(W / w, H / h)
    bw, bh = w * k, h * k
    box = (round((W - bw) / 2), round((H - bh) / 2), round((W + bw) / 2), round((H + bh) / 2))
    return im.crop(box).resize((w, h), Image.LANCZOS)


def face(w, h):
    """The stock tile: a green to purple wash with a smiling face."""
    s = 4
    im = Image.new("RGB", (w * s, h * s))
    d = ImageDraw.Draw(im)
    top, bottom = (0x14, 0xF1, 0x95), (0x99, 0x45, 0xFF)
    for y in range(h * s):
        t = y / (h * s - 1)
        d.line((0, y, w * s, y), fill=tuple(round(a + (b - a) * t) for a, b in zip(top, bottom)))
    side = min(w, h) * s
    eye, gap, mouth, line = side * 9 // 86, side * 20 // 86, side * 36 // 86, side * 4 // 86
    cx, cy = w * s // 2, h * s // 2
    for sign in (-1, 1):
        ex, ey = cx + sign * (gap + eye) // 2, cy - side * 9 // 86
        d.ellipse((ex - eye // 2, ey - eye // 2, ex + eye // 2, ey + eye // 2), fill="white")
    my = cy + side // 86
    d.arc((cx - mouth // 2, my - mouth // 2, cx + mouth // 2, my + mouth // 2), 0, 180, fill="white", width=line)
    return im.resize((w, h), Image.LANCZOS)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    default = "--default" in sys.argv
    if default == bool(args):
        sys.exit(__doc__)
    src = None if default else Image.open(args[0]).convert("RGB")
    for name, w, h, radius in TILES:
        im = face(w, h) if default else crop(src, w, h, "--circle" in sys.argv)
        mask = Image.new("L", (w * 4, h * 4), 0)
        ImageDraw.Draw(mask).rounded_rectangle((0, 0, w * 4 - 1, h * 4 - 1), radius * 4, fill=255)
        # Opaque (a third smaller than with an alpha channel): the rounded
        # corners are blended into the Streaming theme's background colour.
        im = Image.composite(im, Image.new("RGB", (w, h), (0x0B, 0x0B, 0x0D)), mask.resize((w, h), Image.LANCZOS))
        with tempfile.TemporaryDirectory() as tmp:
            png = os.path.join(tmp, name + ".png")
            im.save(png)
            subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "img2lvgl565.py"), png, name,
                                   os.path.join(ROOT, "main", "assets", name + ".c")])


if __name__ == "__main__":
    main()
