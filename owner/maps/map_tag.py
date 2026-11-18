"""Native-style map preview: the room screens show each map as one 120x83 sprite whose top 19 rows carry the map's
English name as a glowing pixel-font tag and whose bottom 64 rows are a scene picture (shipped maps keep these
cells in the client's preview atlas, `txrs/6268.txr`).

The tag is rebuilt the way the shipped tags are drawn: white pixel glyphs (the only opaque pixels) over a cyan
glow whose opacity is the glyph mask blurred (sigma 2.1, gain 1.8, capped at 75 %); everything else is transparent
and coloured magenta, the client's colour key. Glyphs are harvested at compile time from the target client's own
atlas (the name tags of shipped maps, listed below by atlas cell); characters that no shipped tag contains use a
small built-in fallback font. No client artwork is stored in this module.
"""
from __future__ import annotations

import io
from typing import Callable

from PIL import Image, ImageFilter

ATLAS = 'txrs/6268.txr'
SPRITE_SIZE = (120, 83)
TAG_ROWS = 19
PICTURE_SIZE = (120, 64)
GLOW_RGB = (0, 185, 255)
GLOW_SIGMA, GLOW_GAIN, GLOW_CAP = 2.1, 1.8, 0.75
LETTER_GAP, WORD_GAP = 1, 5
KEY = (255, 0, 255)

# shipped atlas layout: cells every 140 x 93 pixels from (20, 10); the tag is the first 19 rows of a cell
_CELL_X, _CELL_DX, _CELL_Y, _CELL_DY = 20, 140, 10, 93
_TAGS = {
    (5, 0): 'AFRICA DESERT', (6, 0): 'ARIZONA BASE', (2, 1): 'SUBMARINE BASE', (3, 1): 'SNOW FIELD',
    (5, 1): 'COLONY CITY', (6, 1): 'NEO TOKYO', (1, 2): 'GUNDAM FIGHT', (6, 2): 'CYCLOPS 51',
    (1, 3): 'MOON WRECK', (5, 3): 'GRAND CANYON', (6, 3): 'RUIN CITY 2', (3, 4): 'JABURO BASE',
    (4, 4): 'COLONY DOCK', (6, 4): 'AMAZON JUNGLE', (3, 5): 'HK DOWNTOWN', (6, 5): 'ONOGORO ISLAND',
    (3, 6): 'LUNAR CITY', (4, 3): 'NEO CHINA',
}

Glyph = list[list[bool]]          # TAG_ROWS rows of column flags


def _white(px) -> bool:
    return px[0] > 200 and px[1] > 200 and px[2] > 200 and px[3] > 200


def harvest(atlas: Image.Image) -> dict[str, Glyph]:
    """Glyph masks of the shipped name tags: per tag, split the white pixels at empty columns and keep the cell
    only when the pieces match its known text."""
    rgba = atlas.convert('RGBA')
    if rgba.size != (1024, 1024):
        return {}
    pixels = rgba.load()
    glyphs: dict[str, Glyph] = {}
    for (cx, cy), text in _TAGS.items():
        x0, y0 = _CELL_X + _CELL_DX * cx, _CELL_Y + _CELL_DY * cy
        mask = [[_white(pixels[x0 + x, y0 + y]) for x in range(SPRITE_SIZE[0])] for y in range(TAG_ROWS)]
        runs, start = [], None
        for x in range(SPRITE_SIZE[0] + 1):
            on = x < SPRITE_SIZE[0] and any(mask[y][x] for y in range(TAG_ROWS))
            if on and start is None:
                start = x
            elif not on and start is not None:
                runs.append((start, x))
                start = None
        chars = [c for c in text if c != ' ']
        if len(runs) != len(chars):
            continue
        for ch, (a, b) in zip(chars, runs):
            glyphs.setdefault(ch, [row[a:b] for row in mask])
    return glyphs


def native_glyphs(read: Callable[[str], bytes] | None) -> dict[str, Glyph]:
    """Glyphs from the target client's preview atlas, or {} when it cannot be read."""
    if read is None:
        return {}
    try:
        raw = read(ATLAS)
    except Exception:
        return {}
    if raw.startswith(b'ZOATEX1\0'):
        raw = raw[12:]
    try:
        return harvest(Image.open(io.BytesIO(raw)))
    except Exception:
        return {}


# Fallback pixel font in the shipped style (2-pixel strokes, 8 rows on tag rows 5..12) for characters that no
# shipped tag contains, or every character when the client atlas cannot be read.
_FONT_ROW = 5
_FONT = {
    'A': '.####.|##..##|##..##|##..##|######|##..##|##..##|##..##',
    'B': '#####.|##..##|##..##|#####.|##..##|##..##|##..##|#####.',
    'C': '.####.|##..##|##....|##....|##....|##....|##..##|.####.',
    'D': '#####.|##..##|##..##|##..##|##..##|##..##|##..##|#####.',
    'E': '######|##....|##....|#####.|##....|##....|##....|######',
    'F': '######|##....|##....|#####.|##....|##....|##....|##....',
    'G': '.####.|##..##|##....|##.###|##..##|##..##|##..##|.#####',
    'H': '##..##|##..##|##..##|######|##..##|##..##|##..##|##..##',
    'I': '##|##|##|##|##|##|##|##',
    'J': '....##|....##|....##|....##|....##|....##|##..##|.####.',
    'K': '##..##|##..##|##.##.|####..|##.##.|##..##|##..##|##..##',
    'L': '##....|##....|##....|##....|##....|##....|##....|######',
    'M': '##...##|###.###|#######|##.#.##|##...##|##...##|##...##|##...##',
    'N': '##..##|###.##|###.##|##.###|##.###|##..##|##..##|##..##',
    'O': '.####.|##..##|##..##|##..##|##..##|##..##|##..##|.####.',
    'P': '#####.|##..##|##..##|#####.|##....|##....|##....|##....',
    'Q': '.####.|##..##|##..##|##..##|##..##|##..##|##.###|.#####',
    'R': '#####.|##..##|##..##|#####.|##.##.|##..##|##..##|##..##',
    'S': '.#####|##....|##....|.####.|....##|....##|....##|#####.',
    'T': '######|..##..|..##..|..##..|..##..|..##..|..##..|..##..',
    'U': '##..##|##..##|##..##|##..##|##..##|##..##|##..##|.####.',
    'V': '##..##|##..##|##..##|##..##|##..##|##..##|.####.|..##..',
    'W': '##.##.##|##.##.##|##.##.##|##.##.##|##.##.##|##.##.##|##.##.##|.######.',
    'X': '##..##|##..##|.####.|..##..|.####.|##..##|##..##|##..##',
    'Y': '##..##|##..##|##..##|##..##|##..##|.####.|..##..|..##..',
    'Z': '######|....##|...##.|..##..|.##...|##....|##....|######',
    '0': '.####.|##..##|##.###|###.##|##..##|##..##|##..##|.####.',
    '1': '.##|###|.##|.##|.##|.##|.##|.##',
    '2': '#####.|....##|....##|.#####|##....|##....|##....|######',
    '3': '#####.|....##|....##|.####.|....##|....##|....##|#####.',
    '4': '##..##|##..##|##..##|######|....##|....##|....##|....##',
    '5': '.#####|##....|##....|######|....##|....##|....##|#####.',
    '6': '.####.|##....|##....|#####.|##..##|##..##|##..##|.####.',
    '7': '######|....##|...##.|..##..|..##..|..##..|..##..|..##..',
    '8': '.####.|##..##|##..##|.####.|##..##|##..##|##..##|.####.',
    '9': '.####.|##..##|##..##|##..##|.#####|....##|....##|.####.',
    '-': '....|....|....|####|....|....|....|....',
    '.': '..|..|..|..|..|..|##|##',
    "'": '##|##|..|..|..|..|..|..',
    '(': '.##|##.|##.|##.|##.|##.|##.|.##',
    ')': '##.|.##|.##|.##|.##|.##|.##|##.',
    ':': '..|##|##|..|..|##|##|..',
    '!': '##|##|##|##|##|..|##|##',
    '?': '.####.|##..##|....##|...##.|..##..|......|..##..|..##..',
    '&': '.###..|##.##.|##.##.|.###..|##.###|##..#.|##.##.|.##.##',
}


def _fallback(ch: str) -> Glyph:
    rows = _FONT.get(ch, _FONT['?']).split('|')
    width = len(rows[0])
    glyph = [[False] * width for _ in range(TAG_ROWS)]
    for y, row in enumerate(rows):
        for x, c in enumerate(row):
            glyph[_FONT_ROW + y][x] = c == '#'
    return glyph


def tag(text: str, glyphs: dict[str, Glyph]) -> Image.Image:
    """The 120x19 RGBA name tag of `text` (upper-cased, centred, condensed when too long)."""
    words = [w for w in text.upper().split() if w]
    if not words:
        raise ValueError('map tag text is empty')
    shapes = [[glyphs.get(ch) or _fallback(ch) for ch in word] for word in words]
    letter_gap, word_gap = LETTER_GAP, WORD_GAP
    width = lambda lg, wg: (sum(len(g[0]) for w in shapes for g in w) + lg * sum(len(w) - 1 for w in shapes)
                            + wg * (len(shapes) - 1))
    while width(letter_gap, word_gap) > SPRITE_SIZE[0] - 6 and word_gap > 3:
        word_gap -= 1
    while width(letter_gap, word_gap) > SPRITE_SIZE[0] - 6 and letter_gap > 0:
        letter_gap -= 1
    total = width(letter_gap, word_gap)
    mask = Image.new('L', (max(total, 1), TAG_ROWS), 0)
    px = mask.load()
    x = 0
    for wi, word in enumerate(shapes):
        for gi, glyph in enumerate(word):
            for yy in range(TAG_ROWS):
                for xx, on in enumerate(glyph[yy]):
                    if on:
                        px[x + xx, yy] = 255
            x += len(glyph[0]) + (letter_gap if gi + 1 < len(word) else 0)
        x += word_gap if wi + 1 < len(shapes) else 0
    if mask.width > SPRITE_SIZE[0] - 4:                       # still too long: squeeze horizontally
        mask = mask.resize((SPRITE_SIZE[0] - 4, TAG_ROWS), Image.NEAREST)
    text_mask = Image.new('L', (SPRITE_SIZE[0], TAG_ROWS), 0)
    text_mask.paste(mask, ((SPRITE_SIZE[0] - mask.width + 1) // 2, 0))
    glow = text_mask.filter(ImageFilter.GaussianBlur(GLOW_SIGMA)).point(
        lambda v: int(min(GLOW_CAP, v / 255.0 * GLOW_GAIN) * 255 + 0.5))
    out = Image.new('RGBA', (SPRITE_SIZE[0], TAG_ROWS), (*KEY, 0))
    out.paste(Image.new('RGBA', out.size, (*GLOW_RGB, 255)), (0, 0), glow)
    glow_px, text_px, out_px = glow.load(), text_mask.load(), out.load()
    for yy in range(TAG_ROWS):
        for xx in range(SPRITE_SIZE[0]):
            if text_px[xx, yy]:
                out_px[xx, yy] = (255, 255, 255, 255)
            elif glow_px[xx, yy]:
                out_px[xx, yy] = (*GLOW_RGB, glow_px[xx, yy])
    return out


def picture(scene: Image.Image) -> Image.Image:
    """Centre-crop the scene to the 120:64 picture area and scale it down."""
    scene = scene.convert('RGB')
    w, h = scene.size
    target = PICTURE_SIZE[0] / PICTURE_SIZE[1]
    if w / h > target:
        cw = round(h * target)
        scene = scene.crop(((w - cw) // 2, 0, (w - cw) // 2 + cw, h))
    else:
        ch = round(w / target)
        scene = scene.crop((0, (h - ch) // 2, w, (h - ch) // 2 + ch))
    return scene.resize(PICTURE_SIZE, Image.LANCZOS)


def preview_sprite(scene: Image.Image, name_en: str, glyphs: dict[str, Glyph]) -> Image.Image:
    """The 120x83 RGBA preview sprite: name tag over the scene picture, transparent elsewhere."""
    sprite = Image.new('RGBA', SPRITE_SIZE, (*KEY, 0))
    sprite.alpha_composite(tag(name_en, glyphs), (0, 0))
    sprite.paste(picture(scene).convert('RGBA'), (0, TAG_ROWS))
    return sprite
