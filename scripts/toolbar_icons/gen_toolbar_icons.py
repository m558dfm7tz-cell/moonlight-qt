#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Generate the Ferry remote toolbar button icons as 4x-scale PNG assets.

Each icon is drawn on a 512x512 RGBA canvas (16x logical size) with PIL and
then downscaled to 128x128 (4x of a 32 px logical cell) with LANCZOS, which
yields smooth anti-aliased edges. At runtime Moonlight loads these PNGs and
scales them to the actual physical size (logical_size * devicePixelRatio).

Output: app/res/toolbar/*.png  (registered in app/resources.qrc)
"""

import os
from PIL import Image, ImageDraw, ImageFont

SS = 512          # supersample canvas (16x logical)
OUT = 128         # output size (4x logical)
OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "app", "res", "toolbar")

WHITE = (241, 246, 253, 255)     # TextPrimary
LIGHT = (214, 224, 235, 255)     # drag dots / soft white
GRAY  = (149, 168, 185, 255)     # TextMuted (disabled)
RED   = (229, 72, 77, 255)       # close X (#E5484D)

STROKE = 34       # line stroke at 512 (approx 2.1 px at 32 logical)


def font(size):
    for p in (r"C:/Windows/Fonts/segoeuib.ttf",
              r"C:/Windows/Fonts/arialbd.ttf",
              r"C:/Windows/Fonts/segoeui.ttf"):
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def new_canvas():
    return Image.new("RGBA", (SS, SS), (0, 0, 0, 0))


def render(draw_fn):
    img = new_canvas()
    d = ImageDraw.Draw(img)
    draw_fn(d)
    img = img.resize((OUT, OUT), Image.LANCZOS)
    return img


def save(img, name):
    path = os.path.join(OUT_DIR, name)
    img.save(path)
    print("wrote", path)


def cx():
    return SS // 2


# ---------------------------------------------------------------- icons

def icon_drag(d):
    # 6-dot grip: 3 cols x 2 rows, light gray dots.
    r = 32
    dx, dy = 72, 62
    for col in range(3):
        for row in range(2):
            x = SS // 2 - dx + col * dx
            y = SS // 2 - dy // 2 + row * dy
            d.ellipse([x - r, y - r, x + r, y + r], fill=LIGHT)


def icon_fullscreen(d):
    # Four outward corner brackets (short Ls, clear gaps between them).
    m = 132
    L = 76
    w = STROKE
    c = SS // 2
    corners = [
        (c - m, c - m, +1, +1),
        (c + m, c - m, -1, +1),
        (c - m, c + m, +1, -1),
        (c + m, c + m, -1, -1),
    ]
    for ox, oy, sx, sy in corners:
        # horizontal arm
        d.line([ox, oy, ox + sx * L, oy], fill=WHITE, width=w)
        # vertical arm
        d.line([ox, oy, ox, oy + sy * L], fill=WHITE, width=w)


def _eye_lens(d, fill=None, width=STROKE):
    # Almond/lens eye: pointed at left/right, curved top/bottom.
    c = SS // 2
    rx, ry = 168, 96
    box = [c - rx, c - ry, c + rx, c + ry]
    d.arc(box, 200, 340, fill=WHITE if fill is None else fill, width=width)   # top
    d.arc(box, 20, 160, fill=WHITE if fill is None else fill, width=width)    # bottom


def icon_privacy(d):
    # Eye outline + diagonal slash.
    _eye_lens(d)
    c = SS // 2
    pr = 24
    d.ellipse([c - pr, c - pr, c + pr, c + pr], fill=WHITE)
    d.line([c - 130, c + 130, c + 130, c - 130], fill=WHITE, width=STROKE)


def icon_privacy_on(d):
    # Solid filled eye + diagonal slash (privacy ON).
    c = SS // 2
    rx, ry = 168, 96
    # filled lens
    d.ellipse([c - rx, c - ry, c + rx, c + ry], fill=WHITE)
    # punch a pupil hole
    pr = 30
    d.ellipse([c - pr, c - pr, c + pr, c + pr], fill=(0, 0, 0, 0))
    # slash
    d.line([c - 130, c + 130, c + 130, c - 130], fill=WHITE, width=STROKE)


def icon_hd(d):
    # Thin rounded rect outline + bold "HD" text.
    c = SS // 2
    pad = 40
    r = 64
    d.rounded_rectangle([pad, pad, SS - pad, SS - pad], radius=r,
                        outline=WHITE, width=STROKE)
    f = font(150)
    txt = "HD"
    bbox = d.textbbox((0, 0), txt, font=f)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    d.text((c - tw // 2 - bbox[0], c - th // 2 - bbox[1]), txt, font=f, fill=WHITE)


def _speaker(d, fill, width=STROKE):
    # Right-facing speaker: cone + box, centered.
    c = SS // 2
    # cone (trapezoid) and box
    pts = [
        (c - 60, c - 96),   # cone back-top
        (c - 130, c - 150),  # cone front-top
        (c - 130, c + 150),  # cone front-bottom
        (c - 60, c + 96),   # cone back-bottom
        (c - 60, c - 96),
    ]
    d.line(pts, fill=fill, width=width, joint="curve")
    # box
    d.line([c - 60, c - 96, c + 60, c - 96], fill=fill, width=width)
    d.line([c + 60, c - 96, c + 60, c + 96], fill=fill, width=width)
    d.line([c + 60, c + 96, c - 60, c + 96], fill=fill, width=width)


def icon_audio(d):
    # Speaker + two sound-wave arcs (local sound playing).
    _speaker(d, WHITE)
    c = SS // 2
    for rr in (84, 130):
        d.arc([c + 70 - rr, c - rr, c + 70 + rr, c + rr], -70, 70,
              fill=WHITE, width=STROKE)


def icon_audio_muted(d):
    # Speaker + diagonal slash (local muted).
    _speaker(d, WHITE)
    c = SS // 2
    d.line([c - 150, c + 150, c + 150, c - 150], fill=WHITE, width=STROKE)


def icon_audio_disabled(d):
    # Speaker + slash, gray (sound stays on host -> disabled).
    _speaker(d, GRAY)
    c = SS // 2
    d.line([c - 150, c + 150, c + 150, c - 150], fill=GRAY, width=STROKE)


def icon_transfer(d):
    # Up arrow out of a tray.
    c = SS // 2
    w = STROKE
    d.line([c, c + 40, c, c - 130], fill=WHITE, width=w)             # stem
    d.line([c - 96, c - 46, c, c - 130], fill=WHITE, width=w)        # left head
    d.line([c, c - 130, c + 96, c - 46], fill=WHITE, width=w)        # right head
    d.line([c - 130, c + 130, c + 130, c + 130], fill=WHITE, width=w)  # tray


def icon_close(d):
    # Red cross.
    c = SS // 2
    m = 100
    d.line([c - m, c - m, c + m, c + m], fill=RED, width=STROKE)
    d.line([c + m, c - m, c - m, c + m], fill=RED, width=STROKE)


def icon_chevron_down(d):
    # Down chevron (collapsed -> expand).
    c = SS // 2
    w = STROKE
    d.line([c - 110, c - 30, c, c + 70], fill=WHITE, width=w)
    d.line([c, c + 70, c + 110, c - 30], fill=WHITE, width=w)


def icon_dock_collapse(d):
    # Double chevrons pointing left (dock-right collapse).
    c = SS // 2
    w = STROKE
    d.line([c - 10, c - 110, c - 90, c], fill=WHITE, width=w)
    d.line([c - 90, c, c - 10, c + 110], fill=WHITE, width=w)
    d.line([c + 70, c - 110, c - 10, c], fill=WHITE, width=w)
    d.line([c - 10, c, c + 70, c + 110], fill=WHITE, width=w)


ICONS = {
    "toolbar_drag.png": icon_drag,
    "toolbar_fullscreen.png": icon_fullscreen,
    "toolbar_privacy.png": icon_privacy,
    "toolbar_privacy_on.png": icon_privacy_on,
    "toolbar_hd.png": icon_hd,
    "toolbar_audio.png": icon_audio,
    "toolbar_audio_muted.png": icon_audio_muted,
    "toolbar_audio_disabled.png": icon_audio_disabled,
    "toolbar_transfer.png": icon_transfer,
    "toolbar_close.png": icon_close,
    "toolbar_collapse.png": icon_chevron_down,
    "toolbar_dock_collapse.png": icon_dock_collapse,
}


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    for name, fn in ICONS.items():
        save(render(fn), name)
    print("done: %d icons" % len(ICONS))


if __name__ == "__main__":
    main()
