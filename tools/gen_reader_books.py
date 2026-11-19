#!/usr/bin/env python3
"""Build the Reader app's bundled files (docs/examples/reader).

    python3 tools/gen_reader_books.py                 # books, covers, font metrics
    python3 tools/gen_reader_books.py --emulator      # ... and copy the app to emulator/local/apps/books
    python3 tools/gen_reader_books.py --add my.txt --title "My Book" --author "Me"

What it writes into the app folder:

    books/<id>.txt     the text: one paragraph per line, "# Kicker | Title" starts a
                       chapter, a line that begins with a space is verse (kept as a
                       line of its own), an empty line is a small gap. ASCII only:
                       the badge's fonts have no other glyphs.
    books/<id>.json    title, author, year, about, words, cover colours and the
                       chapters as byte offsets into the text
    covers/<id>.png    88 x 132 cover, covers/<id>_s.png 60 x 90 (bundled books only)
    lib/metrics.lua    glyph widths of the OS fonts (main/assets/sol_*.c), which the
                       app wraps lines with: Lua apps can't measure text

The bundled books are public-domain texts from Project Gutenberg, fetched into
build/reader-cache/ and stripped of the Project Gutenberg header and licence.
Needs Pillow for the covers.
"""
import argparse
import json
import math
import re
import shutil
import statistics
import sys
import unicodedata
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "docs" / "examples" / "reader"
CACHE = ROOT / "build" / "reader-cache"
FONTS = {12: "sol_mono_12", 14: "sol_font_14", 20: "sol_font_20"}
PART_MAX = 250 * 1024

SMALL = {"of", "the", "a", "an", "and", "or", "in", "at", "to", "for", "was", "is", "it"}


def title_case(s):
    words = s.lower().split()
    out = []
    for i, w in enumerate(words):
        if i and w in SMALL:
            out.append(w)
        elif w.startswith("dr.") or w.startswith("mr."):
            out.append(w.capitalize())
        else:
            out.append(w[0].upper() + w[1:])
    return " ".join(out)


# ---------------------------------------------------------------------------
# The bundled books. `head(block_lines)` returns (kicker, title) for a chapter
# heading block, else None; the body starts at the first heading (or at the
# block after one matching `after`).

def head_alice(lines):
    m = re.match(r"^CHAPTER ([IVXL]+)\.$", lines[0])
    if m and len(lines) == 2:
        return "Chapter " + m.group(1), lines[1].strip()


def head_time(lines):
    m = re.match(r"^ ([IVXL]+)\.$", lines[0])
    if m and len(lines) == 2:
        return "Chapter " + m.group(1), lines[1].strip()
    if len(lines) == 1 and lines[0].strip() == "Epilogue":
        return None, "Epilogue"


JEKYLL = ["STORY OF THE DOOR", "SEARCH FOR MR. HYDE", "DR. JEKYLL WAS QUITE AT EASE", "THE CAREW MURDER CASE",
          "INCIDENT OF THE LETTER", "INCIDENT OF DR. LANYON", "INCIDENT AT THE WINDOW", "THE LAST NIGHT",
          "DR. LANYON'S NARRATIVE", "HENRY JEKYLL'S FULL STATEMENT OF THE CASE"]


def head_jekyll(lines):
    if len(lines) == 1 and lines[0].replace("’", "'") in JEKYLL:
        return None, title_case(lines[0].replace("’", "'"))


def head_carol(lines):
    m = re.match(r"^STAVE ([IVX]+):\s+(.*)$", lines[0])
    if m and len(lines) == 1:
        return "Stave " + m.group(1), title_case(m.group(2))


MEDITATIONS = {"I": "Of Things Doubtful", "II": "Of the Nature of Man's Mind", "III": "Of God, and that there is a God",
               "IV": "Of Truth and Falsehood", "V": "Of the Essence of Things Material",
               "VI": "Of Corporeal Beings and their Existence"}


def head_descartes(lines, nxt):
    m = re.match(r"^MEDITAT\. ([IVX]+)\.$", lines[0])
    if m and len(lines) == 1:
        return "Meditation " + m.group(1), MEDITATIONS[m.group(1)], 2      # the next block is its title


def head_hume(lines, nxt):
    m = re.match(r"^SECTION ([IVX]+)\.$", lines[0])
    if m and len(lines) == 1:
        return "Section " + m.group(1), title_case(re.sub(r"\[\d+\]", "", " ".join(nxt)).strip(". ")), 2


def head_kant(lines, nxt, after=()):
    if lines == ["PREFACE"]:
        return None, "Preface"
    m = re.match(r"^\s*(FIRST|SECOND|THIRD) SECTION$", lines[0])
    if m and len(lines) == 1:
        title = " ".join(l.strip() for b in [nxt] + list(after[:1]) for l in b)
        return m.group(1).capitalize() + " Section", title_case(title.replace("TRANSITION ", "")), 3


def head_mill(lines, nxt):
    m = re.match(r"^CHAPTER ([IVX]+)\.$", lines[0])
    if m and len(lines) == 1:
        return "Chapter " + m.group(1), title_case(" ".join(nxt).strip(". ")), 2


SCHOPENHAUER = ["ON THE SUFFERINGS OF THE WORLD.", "THE VANITY OF EXISTENCE.", "ON SUICIDE.",
                "IMMORTALITY:[1] A DIALOGUE.", "FURTHER PSYCHOLOGICAL OBSERVATIONS.", "ON EDUCATION.", "OF WOMEN.",
                "ON NOISE.", "A FEW PARABLES."]


def head_schopenhauer(lines, nxt):
    if len(lines) == 1 and lines[0] in SCHOPENHAUER:
        return None, title_case(lines[0].replace("[1]", "").strip("."))


def head_nietzsche(lines, nxt):
    m = re.match(r"^CHAPTER ([IVX]+)\. (.*)$", lines[0])
    if m and len(lines) == 1:
        return "Chapter " + m.group(1), title_case(m.group(2))
    if lines in (["PREFACE"], ["FROM THE HEIGHTS"]):
        return None, title_case(lines[0])


BOOKS = [
    dict(id="alice", pg=11, title="Alice's Adventures in Wonderland", author="Lewis Carroll", year=1865, head=head_alice,
         about="Alice follows a White Rabbit down a hole and into a world where nothing behaves, least of all the "
               "grown-ups. The original nonsense classic.",
         bg=0x0E4D5B, ink=0xF4E9D0),
    dict(id="time-machine", pg=35, title="The Time Machine", author="H. G. Wells", year=1895, head=head_time,
         about="A Victorian inventor rides his machine to the year 802,701 and finds what became of mankind. The "
               "book that invented time travel.",
         bg=0x15142B, ink=0xFF8A2A),
    dict(id="jekyll-hyde", pg=43, title="The Strange Case of Dr. Jekyll and Mr. Hyde", author="Robert Louis Stevenson",
         year=1886, head=head_jekyll,
         about="A London lawyer looks into the hold the brutal Mr. Hyde has over his friend, the respectable "
               "Dr. Jekyll. A short, dark thriller.",
         bg=0x8E1B1B, ink=0xF1E6D2),
    dict(id="christmas-carol", pg=46, title="A Christmas Carol", author="Charles Dickens", year=1843, head=head_carol,
         about="Ebenezer Scrooge, a miser, is visited by three spirits on Christmas Eve and given one night to "
               "change. A ghost story in five staves.",
         bg=0x12422B, ink=0xE9C46A),
    dict(id="yellow-wallpaper", pg=1952, title="The Yellow Wallpaper", author="Charlotte Perkins Gilman", year=1892,
         head=lambda lines: None, after=r"^By Charlotte Perkins Gilman",
         about="Confined to an upstairs room for a rest cure, a woman starts to see something moving behind the "
               "wallpaper. A short story.",
         bg=0xE2B33C, ink=0x1F1A10),
    # Philosophy, oldest first. `band` is the cover's colour, `mark` its emblem.
    dict(id="meditations", pg=70091, title="Meditations on First Philosophy", author="Rene Descartes", year=1641,
         head=head_descartes, stop=r"^_?FINIS", band=0x1F3A5F, mark="cogito", cover=["Meditations", "on First", "Philosophy"],
         about="Six days of doubting everything that can be doubted, to find what is left: I think, therefore I am. "
               "In William Molyneux's English of 1680.",
         bg=0x1F3A5F, ink=0xEFE6D2),
    dict(id="enquiry", pg=9662, title="An Enquiry Concerning Human Understanding", author="David Hume", year=1748,
         head=head_hume, stop=r"^INDEX$", band=0x7A3B1E, mark="billiards", cover=["An Enquiry", "Concerning", "Human", "Understanding"],
         about="What can experience teach us? Hume on cause and effect, habit, free will and miracles: the book that "
               "woke Kant from his dogmatic slumber.",
         bg=0x7A3B1E, ink=0xEFE6D2),
    dict(id="metaphysic-morals", pg=5682, title="Fundamental Principles of the Metaphysic of Morals",
         author="Immanuel Kant", year=1785, head=head_kant, band=0x2F4F3A, mark="star",
         cover=["Metaphysic", "of Morals"],
         about="Kant looks for the supreme principle of morality and finds the categorical imperative: act only on a "
               "maxim you could will to be a universal law.",
         bg=0x2F4F3A, ink=0xEFE6D2),
    dict(id="pessimism", pg=10732, title="Studies in Pessimism", author="Arthur Schopenhauer", year=1851,
         head=head_schopenhauer, band=0x3B3340, mark="descent", cover=["Studies in", "Pessimism"],
         about="Nine essays from Parerga and Paralipomena, on suffering, the vanity of existence, suicide, noise and "
               "more. Schopenhauer at his most quotable.",
         bg=0x3B3340, ink=0xEFE6D2),
    dict(id="utilitarianism", pg=11224, title="Utilitarianism", author="John Stuart Mill", year=1863,
         head=head_mill, band=0x8A6A1F, mark="scales", cover=["Utilitarianism"],
         about="Actions are right as they tend to promote happiness, wrong as they tend to produce the reverse. "
               "Mill's defence of the greatest happiness principle.",
         bg=0x8A6A1F, ink=0xEFE6D2),
    dict(id="beyond-good-evil", pg=4363, title="Beyond Good and Evil", author="Friedrich Nietzsche", year=1886,
         head=head_nietzsche, band=0x8E1F2B, mark="bolt", cover=["Beyond", "Good and", "Evil"],
         about="296 aphorisms against the prejudices of philosophers, on the free spirit, the will to power and "
               "master and slave morality. Helen Zimmern's translation.",
         bg=0x8E1F2B, ink=0xEFE6D2),
]


def fetch(pg):
    CACHE.mkdir(parents=True, exist_ok=True)
    path = CACHE / f"pg{pg}.txt"
    if not path.exists():
        url = f"https://www.gutenberg.org/cache/epub/{pg}/pg{pg}.txt"
        print("fetching", url)
        req = urllib.request.Request(url, headers={"User-Agent": "book-fetch/1.0"})
        with urllib.request.urlopen(req, timeout=60) as r:
            path.write_bytes(r.read())
    return path.read_text(encoding="utf-8-sig")


# ---------------------------------------------------------------------------
# Text

TRANSLIT = {"“": '"', "”": '"', "‘": "'", "’": "'", "…": "...", "æ": "ae",
            "Æ": "AE", "œ": "oe", "Œ": "OE", "™": "", "•": "*", " ": " ",
            "–": "-", "£": "GBP ", "×": "x"}


def ascii_text(s):
    s = re.sub(r"(?<=\w)\s*(?:—|--+)\s*(?=\w)", " - ", s)      # a dash between words can wrap
    s = s.replace("—", "--")
    for a, b in TRANSLIT.items():
        s = s.replace(a, b)
    s = unicodedata.normalize("NFKD", s)
    s = "".join(c for c in s if not unicodedata.combining(c))
    s = s.replace("_", "").replace("~", "")                         # _italics_: the badge has one face
    return "".join(c if 32 <= ord(c) < 127 or c == "\n" else "?" for c in s)


def blocks_of(text):
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    return [b.split("\n") for b in re.split(r"\n[ \t]*\n+", text.strip("\n")) if b.strip()]


def call_head(head, blocks, i):
    """(kicker, title, blocks taken) if blocks[i] starts a chapter."""
    n = head.__code__.co_argcount
    args = [blocks[i]] + ([blocks[i + 1] if i + 1 < len(blocks) else []] if n > 1 else []) + ([blocks[i + 2:i + 4]] if n > 2 else [])
    h = head(*args)
    if h:
        return h if len(h) == 3 else (h[0], h[1], 1)


def to_reader(blocks, head, stop=None):
    """Blocks of source lines -> (the app's text, chapters)."""
    lengths = [len(l) for b in blocks for l in b[:-1]]
    short = 0.72 * (statistics.median(lengths) if lengths else 70)
    out, chapters, size = [], [], 0

    def emit(line):
        nonlocal size
        out.append(line)
        size += len(line.encode()) + 1

    def gap():
        if out and out[-1] != "" and not out[-1].startswith("#"):
            emit("")

    skip, note, hanging = 0, False, False
    for i, lines in enumerate(blocks):
        if skip:
            skip -= 1
            continue
        joined = " ".join(l.strip() for l in lines)
        if stop and re.match(stop, joined):
            break
        # Footnotes: "[Footnote 1: ...]" over one block or several, Hume's indented "[1] ..." blocks.
        if note:
            note = not joined.endswith("]")
            continue
        if hanging and all(l.startswith("    ") for l in lines):
            continue
        hanging = bool(re.match(r"^\s+\[\d+\]", lines[0]))
        if hanging:
            continue
        if joined.startswith("[Footnote"):
            note = not joined.endswith("]")
            continue
        if (re.match(r"^\[Illustration.*\]$", joined) or re.match(r"^(THE END|FOOTNOTES|SEC_\d)[.:]?$", joined.strip())):
            continue
        lines = [re.sub(r"\[(\d+|[A-Z])\]", "", l) for l in lines]
        joined = " ".join(l.strip() for l in lines)
        h = call_head(head, blocks, i)
        if h:
            skip = h[2] - 1
            h = h[:2]
            while out and out[-1] == "":                            # no gap before a chapter
                size -= 1
                out.pop()
            kicker, title = h
            title = ascii_text(title)
            chapters.append({"t": title, "k": kicker, "o": size})
            emit("# " + (kicker + " | " if kicker else "") + title)
            continue
        if re.match(r"^[\s*.]+$", joined) and "*" in joined:
            gap()
            emit("* * *")
            emit("")
            continue
        body = lines[:-1]
        verse = len(lines) > 1 and (sum(len(l) < short for l in body) > 0.6 * len(body)
                                    or all(l.startswith("  ") for l in lines))
        if verse:
            gap()
            for l in lines:
                emit(" " + ascii_text(re.sub(r"\s+", " ", l.strip())))
            emit("")
        else:
            emit(ascii_text(re.sub(r"\s+", " ", joined)).strip())
    while out and out[-1] == "":
        out.pop()
    text = "\n".join(out) + "\n"
    if chapters and chapters[0]["o"] != 0:
        chapters.insert(0, {"t": "Beginning", "k": None, "o": 0})
    return text, chapters


def bundled(book):
    raw = fetch(book["pg"])
    m1 = re.search(r"^\*\*\* START OF.*$", raw, re.M)
    m2 = re.search(r"^\*\*\* END OF.*$", raw, re.M)
    blocks = blocks_of(raw[m1.end():m2.start()])
    if book.get("after"):
        start = next(i for i, b in enumerate(blocks) if re.match(book["after"], b[0])) + 1
    else:
        start = next(i for i in range(len(blocks)) if call_head(book["head"], blocks, i))
    return to_reader(blocks[start:], book["head"], book.get("stop"))


def write_book(book, text, chapters):
    d = APP / "books"
    d.mkdir(parents=True, exist_ok=True)
    data = text.encode("ascii")
    for c in chapters:
        assert c["o"] == 0 or data[c["o"] - 1:c["o"] + 1] == b"\n#", c
    # A bundled file is 256 KB at most: a longer book is cut where a chapter starts.
    parts, at = [], 0
    while len(data) - at > PART_MAX:
        cut = max(c["o"] for c in chapters if at < c["o"] <= at + PART_MAX)
        parts.append(data[at:cut])
        at = cut
    parts.append(data[at:])
    for old in d.glob(f"{book['id']}.*.txt"):
        old.unlink()
    for i, part in enumerate(parts):
        (d / (f"{book['id']}.txt" if i == 0 else f"{book['id']}.{i + 1}.txt")).write_bytes(part)
    meta = {"title": book["title"], "author": book.get("author", ""), "year": book.get("year"),
            "about": book.get("about", ""), "words": len(text.split()), "bg": book.get("bg", 0x2A2935),
            "ink": book.get("ink", 0xFFFFFF), "order": book.get("order", 99), "parts": len(parts),
            "chapters": [{k: v for k, v in c.items() if v is not None} for c in chapters]}
    assert len(meta["about"]) <= 200, "about: at most 200 characters (one label)"
    (d / f"{book['id']}.json").write_text(json.dumps(meta, separators=(",", ":")) + "\n")
    print(f"{book['id']:18} {len(data) / 1024:6.1f} KB  {len(chapters):2} chapters  {meta['words']} words")
    return len(data)


# ---------------------------------------------------------------------------
# Font metrics: what LVGL advances per glyph, (adv_w + 8) >> 4, for 0x20..0x7E.

def metrics():
    rows = []
    for size, name in FONTS.items():
        src = (ROOT / "main" / "assets" / f"{name}.c").read_text()
        adv = [int(a) for a in re.findall(r"\.adv_w = (\d+)", src)]
        widths = [(a + 8) >> 4 for a in adv[1:96]]                  # glyph ids 1..95 are 0x20..0x7E
        line = int(re.search(r"\.line_height = (\d+)", src).group(1))
        assert len(widths) == 95 and max(widths) < 70
        enc = "".join(chr(48 + w) for w in widths)
        assert "\\" not in enc and '"' not in enc and "]" not in enc
        rows.append(f"  [{size}] = widths({line}, [[{enc}]]),")
    lua = ("-- Glyph widths of the OS fonts in px, by byte (32..126). Made by\n"
           "-- tools/gen_reader_books.py from main/assets/sol_*.c: don't edit.\n"
           "local function widths(line, s)\n"
           "  local t = {line = line}\n"
           "  for i = 1, #s do t[31 + i] = s:byte(i) - 48 end\n"
           "  return t\n"
           "end\n"
           "return {\n" + "\n".join(rows) + "\n}\n")
    (APP / "lib").mkdir(parents=True, exist_ok=True)
    (APP / "lib" / "metrics.lua").write_text(lua)


# ---------------------------------------------------------------------------
# Covers, drawn at 4x and scaled down.

K = 4
CW, CH = 88 * K, 132 * K
SERIF = "/System/Library/Fonts/Supplemental/Georgia.ttf"
SERIF_B = "/System/Library/Fonts/Supplemental/Georgia Bold.ttf"
SERIF_I = "/System/Library/Fonts/Supplemental/Georgia Italic.ttf"


def rgb(h):
    return (h >> 16 & 255, h >> 8 & 255, h & 255)


def mix(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def text_block(draw, lines, font, cx, y, fill, lead=1.12):
    from PIL import ImageFont
    f = ImageFont.truetype(font[0], font[1] * K)
    for line in lines:
        w = draw.textlength(line, font=f)
        draw.text((cx - w / 2, y), line, font=f, fill=fill)
        y += font[1] * K * lead
    return y


def cover_alice(d, bg, ink):
    # A keyhole, and the heart the Queen is so fond of.
    cx, cy = CW / 2, 122
    d.ellipse((cx - 56, cy - 56, cx + 56, cy + 56), fill=ink)
    d.polygon([(cx - 24, cy + 28), (cx + 24, cy + 28), (cx + 52, cy + 136), (cx - 52, cy + 136)], fill=ink)
    red = (200, 48, 60)
    r = 16
    d.ellipse((cx - 2 * r, cy - r - 8, cx, cy + r - 8), fill=red)
    d.ellipse((cx, cy - r - 8, cx + 2 * r, cy + r - 8), fill=red)
    d.polygon([(cx - 2 * r + 2, cy), (cx + 2 * r - 2, cy), (cx, cy + 38)], fill=red)
    d.rectangle((14, 14, CW - 14, CH - 14), outline=mix(bg, ink, 0.45), width=3)
    text_block(d, ["Alice's", "Adventures in", "Wonderland"], (SERIF_B, 11), cx, 290, ink)
    text_block(d, ["LEWIS CARROLL"], (SERIF, 7), cx, CH - 66, mix(bg, ink, 0.8))


def cover_time(d, bg, ink):
    # Rings running off into the future.
    cx, cy = CW / 2, 150
    for i in range(9):
        r = 14 + i * 16
        t = i / 8
        d.ellipse((cx - r, cy - r * 0.92, cx + r, cy + r * 0.92), outline=mix(ink, bg, t * 0.85), width=max(2, 7 - i // 2))
    d.ellipse((cx - 7, cy - 7, cx + 7, cy + 7), fill=(255, 240, 210))
    text_block(d, ["THE", "TIME", "MACHINE"], (SERIF_B, 11), cx, 306, (245, 236, 220), lead=1.1)
    text_block(d, ["H. G. WELLS"], (SERIF, 7), cx, CH - 58, ink)


def cover_jekyll(d, bg, ink):
    # One face, two halves.
    dark = (20, 16, 18)
    d.rectangle((CW / 2, 0, CW, CH), fill=dark)
    cx, cy, r = CW / 2, 150, 92
    d.pieslice((cx - r, cy - r, cx + r, cy + r), 90, 270, fill=ink)
    d.pieslice((cx - r, cy - r, cx + r, cy + r), 270, 90, fill=bg)
    d.ellipse((cx - 50, cy - 22, cx - 26, cy + 2), fill=bg)
    d.polygon([(cx + 22, cy - 26), (cx + 56, cy - 12), (cx + 26, cy + 2)], fill=dark)
    d.rectangle((cx - 52, cy + 40, cx - 8, cy + 46), fill=bg)
    d.polygon([(cx + 8, cy + 36), (cx + 20, cy + 54), (cx + 30, cy + 36), (cx + 40, cy + 54), (cx + 52, cy + 36)],
              fill=dark)
    text_block(d, ["The Strange Case of"], (SERIF_I, 7), cx, 286, ink)
    text_block(d, ["Dr. Jekyll", "& Mr. Hyde"], (SERIF_B, 13), cx, 322, ink)
    text_block(d, ["R. L. STEVENSON"], (SERIF, 7), cx, CH - 60, mix(dark, ink, 0.8))


def cover_carol(d, bg, ink):
    # A candle in the snow.
    import random
    rnd = random.Random(7)
    snow = (236, 240, 236)
    for _ in range(46):
        x, y, r = rnd.uniform(16, CW - 16), rnd.uniform(16, 300), rnd.choice([3, 3, 4, 6])
        d.ellipse((x - r, y - r, x + r, y + r), fill=mix(bg, snow, rnd.uniform(0.35, 0.9)))
    cx = CW / 2
    d.rectangle((cx - 20, 150, cx + 20, 270), fill=(240, 232, 214))
    d.rectangle((cx - 44, 268, cx + 44, 284), fill=ink)
    d.rectangle((cx - 2, 128, cx + 2, 152), fill=(40, 30, 20))
    d.ellipse((cx - 30, 52, cx + 30, 132), fill=mix(bg, ink, 0.35))
    d.polygon([(cx, 56), (cx + 17, 108), (cx, 132), (cx - 17, 108)], fill=ink)
    d.ellipse((cx - 17, 96, cx + 17, 132), fill=ink)
    d.ellipse((cx - 7, 108, cx + 7, 130), fill=(255, 246, 214))
    text_block(d, ["A Christmas", "Carol"], (SERIF_B, 13), cx, 318, ink)
    text_block(d, ["CHARLES DICKENS"], (SERIF, 7), cx, CH - 60, (236, 240, 236))


def cover_wallpaper(d, bg, ink):
    # The pattern, and the bars behind it.
    dark = mix(bg, (120, 70, 10), 0.55)
    step = 44
    for row in range(-1, 9):
        for col in range(-1, 9):
            x = col * step + (step // 2 if row % 2 else 0)
            y = row * step
            d.arc((x, y, x + step, y + step), 180, 360, fill=dark, width=5)
            d.arc((x + 11, y + 11, x + step - 11, y + step - 11), 180, 360, fill=dark, width=4)
    for x in range(40, CW, 68):
        d.rectangle((x, 0, x + 5, 300), fill=mix(bg, ink, 0.55))
    d.rectangle((0, 296, CW, CH), fill=ink)
    text_block(d, ["The Yellow", "Wallpaper"], (SERIF_B, 12), CW / 2, 322, bg)
    text_block(d, ["C. P. GILMAN"], (SERIF, 7), CW / 2, CH - 60, mix(ink, bg, 0.75))


def cover_idea(d, bg, ink, book):
    # One design for the philosophy shelf: a band of colour with an emblem, the
    # title set large on cream below it.
    cream, dark = (239, 230, 210), (32, 27, 24)
    band = rgb(book["band"])
    d.rectangle((0, 0, CW, CH), fill=cream)
    d.rectangle((0, 0, CW, 210), fill=band)
    cx, cy = CW / 2, 108
    mark = book["mark"]
    if mark == "cogito":                # a point that knows it is one
        for r, w in ((70, 5), (46, 5)):
            d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=cream, width=w)
        d.ellipse((cx - 14, cy - 14, cx + 14, cy + 14), fill=cream)
    elif mark == "billiards":           # one ball strikes another: is that a cause?
        d.ellipse((cx - 92, cy - 34, cx - 24, cy + 34), fill=cream)
        d.ellipse((cx + 24, cy - 34, cx + 92, cy + 34), outline=cream, width=6)
        d.line((cx - 16, cy, cx + 16, cy), fill=cream, width=5)
    elif mark == "star":                # the starry heavens above
        pts = []
        for i in range(10):
            r = 72 if i % 2 == 0 else 30
            a = -math.pi / 2 + i * math.pi / 5
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
        d.polygon(pts, fill=cream)
    elif mark == "descent":
        for i in range(4):
            y = cy - 58 + i * 36
            w = 84 - i * 22
            d.polygon([(cx - w, y), (cx + w, y), (cx, y + 30)], fill=mix(band, cream, 1 - i * 0.22))
    elif mark == "scales":
        d.rectangle((cx - 4, cy - 62, cx + 4, cy + 62), fill=cream)
        d.rectangle((cx - 84, cy - 52, cx + 84, cy - 44), fill=cream)
        for sx in (-84, 84):
            d.pieslice((cx + sx - 34, cy - 30, cx + sx + 34, cy + 38), 0, 180, fill=cream)
        d.rectangle((cx - 40, cy + 58, cx + 40, cy + 66), fill=cream)
    elif mark == "bolt":
        d.polygon([(cx + 22, cy - 82), (cx - 46, cy + 12), (cx - 4, cy + 12), (cx - 26, cy + 82), (cx + 48, cy - 16),
                   (cx + 4, cy - 16)], fill=cream)
    d.rectangle((0, 210, CW, 216), fill=dark)
    lines = book["cover"]
    size = 13 if len(lines) <= 2 and max(map(len, lines)) <= 10 else 11 if len(lines) <= 3 else 10
    if max(map(len, lines)) > 12:
        size = min(size, 9)
    block = len(lines) * size * K * 1.12
    text_block(d, lines, (SERIF_B, size), cx, 216 + (CH - 216 - 76 - block) / 2 + 6, dark)
    text_block(d, [book["author"].split()[-1].upper()], (SERIF, 8), cx, CH - 70, band)


COVERS = {"alice": cover_alice, "time-machine": cover_time, "jekyll-hyde": cover_jekyll,
          "christmas-carol": cover_carol, "yellow-wallpaper": cover_wallpaper}


def covers():
    from PIL import Image, ImageDraw
    d = APP / "covers"
    d.mkdir(parents=True, exist_ok=True)
    total = 0
    for book in BOOKS:
        bg, ink = rgb(book["bg"]), rgb(book["ink"])
        im = Image.new("RGB", (CW, CH), bg)
        if book["id"] in COVERS:
            COVERS[book["id"]](ImageDraw.Draw(im), bg, ink)
        else:
            cover_idea(ImageDraw.Draw(im), bg, ink, book)
        # A spine shadow down the left edge, as on a paperback.
        shade = Image.new("RGB", (CW, CH), (0, 0, 0))
        mask = Image.new("L", (CW, CH), 0)
        md = ImageDraw.Draw(mask)
        md.rectangle((0, 0, 5, CH), fill=70)
        md.rectangle((14, 0, 17, CH), fill=36)
        im = Image.composite(shade, im, mask)
        for suffix, size in (("", (88, 132)), ("_s", (60, 90))):
            small = im.resize(size, Image.LANCZOS).quantize(48, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
            path = d / f"{book['id']}{suffix}.png"
            small.save(path, optimize=True)
            total += path.stat().st_size
    return total


# ---------------------------------------------------------------------------

def sync_emulator():
    """The card's layout: main.lua and app.json, everything else under files/."""
    dst = ROOT / "emulator" / "local" / "apps" / "books"
    if dst.exists():
        shutil.rmtree(dst)
    files = []
    for p in sorted(APP.rglob("*")):
        if p.is_dir() or p.name.startswith(".") or p.name == "README.md":
            continue
        rel = p.relative_to(APP).as_posix()
        out = rel if rel in ("main.lua", "app.json") else "files/" + rel
        (dst / out).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(p, dst / out)
        files.append(out)
    url = "http://127.0.0.1:8765/?app=books&files=" + ",".join(files)
    (dst / "URL.txt").write_text(url + "\n")
    print("emulator:", url)


def store_zip(path, only):
    """The folder as the store takes it (main.lua, app.json, the files beside them), with the books in `only`."""
    import zipfile
    keep, total, count = set(only), 0, 0
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for p in sorted(APP.rglob("*")):
            rel = p.relative_to(APP).as_posix()
            if p.is_dir() or p.name.startswith(".") or p.name == "README.md":
                continue
            if rel.startswith(("books/", "covers/")) and re.sub(r"(_s)?\.(\d+\.)?(txt|json|png)$", "", p.name) not in keep:
                continue
            z.write(p, rel)
            if rel not in ("main.lua", "app.json"):
                total, count = total + p.stat().st_size, count + 1
                assert p.stat().st_size <= 256 * 1024, rel
    print(f"{path}: {count} bundled files, {total / 1024:.0f} KB of 1024 KB")
    assert count <= 64 and total <= 1024 * 1024, "over the store's limits"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--emulator", action="store_true", help="copy the app to emulator/local/apps/books")
    ap.add_argument("--add", metavar="FILE", help="add a plain-text book of your own (blank lines between paragraphs)")
    ap.add_argument("--title")
    ap.add_argument("--author", default="")
    ap.add_argument("--no-covers", action="store_true")
    ap.add_argument("--zip", metavar="FILE", help="write the app as a .zip for the store's submit page")
    ap.add_argument("--only", metavar="IDS", help="with --zip: the books to pack, comma-separated (default: all)")
    args = ap.parse_args()

    if args.add:
        src = Path(args.add)
        title = args.title or src.stem.replace("_", " ").replace("-", " ").title()
        bid = re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-")[:24] or "book"

        def head(lines):                                            # "# Title" or "CHAPTER 3" on a line of its own
            if len(lines) == 1 and lines[0].startswith("# "):
                return None, lines[0][2:].strip()
            if len(lines) <= 2 and re.match(r"^(CHAPTER|Chapter|PART|Part) [0-9IVXLC]+\.?$", lines[0].strip()):
                return lines[0].strip().rstrip("."), (lines[1].strip() if len(lines) == 2 else lines[0].strip())

        text, chapters = to_reader(blocks_of(src.read_text(encoding="utf-8-sig")), head)
        write_book(dict(id=bid, title=ascii_text(title), author=ascii_text(args.author)), text, chapters)
    else:
        metrics()
        for i, b in enumerate(BOOKS):
            b["order"] = i + 1
        total = sum(write_book(b, *bundled(b)) for b in BOOKS)
        if not args.no_covers:
            total += covers()
        print(f"bundled files: {total / 1024:.0f} KB of 1024 KB")
    if args.emulator:
        sync_emulator()
    if args.zip:
        store_zip(Path(args.zip), args.only.split(",") if args.only else [b["id"] for b in BOOKS])


if __name__ == "__main__":
    sys.exit(main())
