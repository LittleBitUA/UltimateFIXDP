#!/usr/bin/env python3
"""Builds gamepad glyph atlases (1024x1024, 128x128 cells, last row 256x128 regions) for Deadly Premonition from the
Kenney Input Prompts pack (CC0, https://kenney.nl/assets/input-prompts).

    python3 make_glyph_atlas.py <path to the unpacked Kenney pack> <output folder>

Writes playstation.png and xbox.png. The cell layout is the game's (see docs/PORT_STATUS.md, glyph atlas)."""
import math, os, sys
from PIL import Image, ImageDraw

CELL = 128
VISIBLE = 120  # icon size inside a cell (about 4 px of padding on each side)


def find(folder, *names):
    for n in names:
        p = os.path.join(folder, n + ".png")
        if os.path.exists(p):
            return p
    raise SystemExit("missing icon: " + " / ".join(names) + " in " + folder)


def icon(folder, *names, box=(VISIBLE, VISIBLE)):
    im = Image.open(find(folder, *names)).convert("RGBA")
    scale = min(box[0] / im.width, box[1] / im.height)
    return im.resize((max(1, round(im.width * scale)), max(1, round(im.height * scale))), Image.LANCZOS)


def put(atlas, im, col, row, spanx=1):
    x = col * CELL + (CELL * spanx - im.width) // 2
    y = row * CELL + (CELL - im.height) // 2
    atlas.alpha_composite(im, (x, y))


def mono_direction(im):
    """The highlighted arm of a D-pad icon is red in the pack: make it white and the rest grey (no colour)."""
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            if r > 170 and g < 120 and b < 130:
                px[x, y] = (255, 255, 255, a)
            elif r > 200 and g > 200 and b > 200:
                px[x, y] = (150, 150, 155, a)
    return im


def spinner(frame):
    """A simple loading wheel; four frames rotated by 90 degrees / 4 steps."""
    im = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for i in range(12):
        a = math.radians(i * 30 + frame * 22.5)
        shade = 90 + int(165 * (i / 11.0))
        x0, y0 = 64 + 34 * math.sin(a), 64 - 34 * math.cos(a)
        x1, y1 = 64 + 52 * math.sin(a), 64 - 52 * math.cos(a)
        d.line([(x0, y0), (x1, y1)], fill=(255, 255, 255, shade), width=7)
    return im


def arrow_down():
    im = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.polygon([(34, 52), (94, 52), (64, 92)], fill=(255, 255, 255, 230))
    d.rectangle([54, 24, 74, 56], fill=(255, 255, 255, 230))
    return im


def build(pack, theme, out):
    if theme == "playstation":
        f = os.path.join(pack, "PlayStation Series", "Double")
        cells = {
            (0, 0): icon(f, "playstation_button_cross"), (0, 1): icon(f, "playstation_button_circle"),
            (0, 2): icon(f, "playstation_button_square"), (0, 3): icon(f, "playstation_button_triangle"),
            (0, 4): icon(f, "playstation_dpad"), (0, 5): icon(f, "playstation_dpad_up"), (0, 6): icon(f, "playstation_dpad_down"),
            (0, 7): icon(f, "playstation_dpad_left"), (1, 0): icon(f, "playstation_dpad_right"),
            (1, 1): icon(f, "playstation_stick_l"), (1, 2): icon(f, "playstation_stick_r"),
            (1, 7): icon(f, "playstation5_button_options", "playstation4_button_options"),
            (2, 0): icon(f, "playstation5_button_create", "playstation4_button_share"),
            (2, 1): icon(f, "playstation_trigger_l1"), (2, 2): icon(f, "playstation_trigger_r1"),
            (2, 3): icon(f, "playstation_trigger_l2"), (2, 4): icon(f, "playstation_trigger_r2"),
            (2, 5): icon(f, "playstation_button_l3"), (2, 6): icon(f, "playstation_button_r3"),
            (2, 7): icon(f, "playstation_dpad_vertical"), (3, 0): icon(f, "playstation_dpad_horizontal"),
        }
        wide = [(0, icon(f, "playstation_stick_l_left", box=(120, 120))), (2, icon(f, "playstation_stick_l_right", box=(120, 120))),
                (4, icon(f, "playstation_stick_r_left", box=(120, 120))), (6, icon(f, "playstation_stick_r_right", box=(120, 120)))]
    else:
        f = os.path.join(pack, "Xbox Series", "Double")
        cells = {
            (0, 0): icon(f, "xbox_button_color_a", "xbox_button_a"), (0, 1): icon(f, "xbox_button_color_b", "xbox_button_b"),
            (0, 2): icon(f, "xbox_button_color_x", "xbox_button_x"), (0, 3): icon(f, "xbox_button_color_y", "xbox_button_y"),
            (0, 4): icon(f, "xbox_dpad"), (0, 5): icon(f, "xbox_dpad_up"), (0, 6): icon(f, "xbox_dpad_down"),
            (0, 7): icon(f, "xbox_dpad_left"), (1, 0): icon(f, "xbox_dpad_right"),
            (1, 1): icon(f, "xbox_stick_l"), (1, 2): icon(f, "xbox_stick_r"),
            (1, 7): icon(f, "xbox_button_menu", "xbox_button_start"), (2, 0): icon(f, "xbox_button_view"),
            (2, 1): icon(f, "xbox_lb"), (2, 2): icon(f, "xbox_rb"), (2, 3): icon(f, "xbox_lt"), (2, 4): icon(f, "xbox_rt"),
            (2, 5): icon(f, "xbox_stick_l_press", "xbox_stick_l"), (2, 6): icon(f, "xbox_stick_r_press", "xbox_stick_r"),
            (2, 7): icon(f, "xbox_dpad_vertical"), (3, 0): icon(f, "xbox_dpad_horizontal"),
        }
        wide = [(0, icon(f, "xbox_stick_l_left", box=(120, 120))), (2, icon(f, "xbox_stick_l_right", box=(120, 120))),
                (4, icon(f, "xbox_stick_r_left", box=(120, 120))), (6, icon(f, "xbox_stick_r_right", box=(120, 120)))]
    if theme == "playstation":
        for key in ((0, 5), (0, 6), (0, 7), (1, 0), (2, 7), (3, 0)):
            cells[key] = mono_direction(cells[key])
    atlas = Image.new("RGBA", (1024, 1024), (0, 0, 0, 0))
    for (r, c), im in cells.items():
        put(atlas, im, c, r)
    for k in range(4):
        atlas.alpha_composite(spinner(k), ((3 + k) * CELL, 1 * CELL))
    atlas.alpha_composite(arrow_down(), (1 * CELL, 3 * CELL))
    for c, im in wide:
        put(atlas, im, c, 7, spanx=2)
    atlas.save(os.path.join(out, theme + ".png"))
    print("wrote", os.path.join(out, theme + ".png"))


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    os.makedirs(sys.argv[2], exist_ok=True)
    for t in ("playstation", "xbox"):
        build(sys.argv[1], t, sys.argv[2])
