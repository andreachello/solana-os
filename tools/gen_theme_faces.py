#!/usr/bin/env python3
# Backgrounds for the Themes store app (docs/examples/themes): the pearl looks'
# wallpaper with their glass cards and artwork already on it, as 320x218
# palette PNGs (the app area; Lua images have no blending, so the art can't be
# layered on the badge). Needs Pillow and the sprites in emulator/local/art.
#   python3 tools/gen_theme_faces.py
import os
import sys
from PIL import Image, ImageDraw

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import gen_wallpaper as gw

ART = os.path.join(here, "..", "emulator", "local", "art")
OUT = os.path.join(here, "..", "docs", "examples", "themes", "art")
W, H, BAR = 320, 218, 22        # the app area sits under the 22 px status bar

# name: (wallpaper, sprite prefix, card fill RGBA, card border RGBA, hero border RGBA)
FACES = {
    "black_pearl": ("black_pearl", "img_black_pearl", (255, 255, 255, 24), (255, 255, 255, 40), (143, 235, 216, 255)),
    "pearl": ("pearl", "img_pearl", (255, 255, 255, 190), (255, 255, 255, 255), (150, 160, 230, 255)),
}
# card: (x, y, w, h), sprite name, sprite offset in the card (emulator/local/art/PLACEMENT.txt)
CARDS = {
    "black_pearl": [((8, 2, 304, 108), "hero", (172, 0)), ((8, 116, 149, 80), "wifi", (85, 26)),
                    ((163, 116, 149, 80), "quest", (48, 9))],
    "pearl": [((8, 2, 304, 108), "hero", (183, 8)), ((8, 116, 149, 80), "wifi", (85, 26)),
              ((163, 116, 149, 80), "quest", (86, 27))],
}

for name, (wall, prefix, fill, border, hero_border) in FACES.items():
    ground, blobs = gw.WALLS[wall]
    px = gw.render(ground, blobs)
    img = Image.new("RGB", (gw.W, gw.H))
    img.putdata([tuple(int(c) for c in p) for p in px])
    img = img.crop((0, BAR, W, BAR + H)).convert("RGBA")
    for i, ((x, y, w, h), sprite, (sx, sy)) in enumerate(CARDS[name]):
        layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)
        d.rounded_rectangle((x, y, x + w - 1, y + h - 1), radius=16, fill=fill,
                            outline=hero_border if i == 0 else border, width=1)
        img = Image.alpha_composite(img, layer)
        art = Image.open(os.path.join(ART, f"{prefix}_{sprite}.png")).convert("RGBA")
        img.alpha_composite(art, (x + sx, y + sy))
    out = img.convert("RGB").quantize(colors=255, dither=Image.FLOYDSTEINBERG)
    path = os.path.join(OUT, name + ".png")
    out.save(path, optimize=True)
    print(path, os.path.getsize(path), "bytes")
