#!/usr/bin/env python3
"""Draws the window background of Alfred's Mac disk image, after OLAAF's installer/mac_dmg.

    make_dmg_background.py <folder with IBMPlexSans-Regular.ttf and IBMPlexSans-Bold.ttf> <output folder> [logo.png]

Writes background.png (640 x 420) and background@2x.png (1280 x 840). The build joins them with
tiffutil -cathidpicheck into .background/background.tiff. The icons sit at (150,160), (330,160), (530,160).
"""
import os, sys
from PIL import Image, ImageDraw, ImageFont

NAME = "Alfred"
SUBTITLE = "A simple Denoiser at your Service"
STEPS = [
    ["Double-click Install Alfred and click through."],
    ["macOS says it can’t be opened? Click Done, then double-click",
     "Blocked? Open Privacy & Security, scroll down and click Open Anyway."],
    ["Double-click Install Alfred again and choose Open."],
]
FOOTER = "Quit your DAW first. The installer is free and not signed by Apple, so macOS asks once."

INK, GRAY, RULE, BLUE, WHITE = (30, 30, 30), (90, 90, 90), (226, 226, 226), (76, 147, 220), (255, 255, 255)


def icon(d, x, y, size, k):
    """Alfred's mark: the power symbol of its title bar, white on a blue rounded square."""
    d.rounded_rectangle([x, y, x + size - 1, y + size - 1], radius=round(size * 0.22), fill=BLUE)
    cx, cy, r, w = x + size / 2, y + size / 2 + size * 0.03, size * 0.25, max(2, round(size * 0.075))
    d.arc([cx - r, cy - r, cx + r, cy + r], start=-58, end=238, fill=WHITE, width=w)
    d.line([cx, cy - r - size * 0.07, cx, cy - size * 0.02], fill=WHITE, width=w)
    d.ellipse([cx - w / 2, cy - r - size * 0.07 - w / 2, cx + w / 2, cy - r - size * 0.07 + w / 2], fill=WHITE)
    d.ellipse([cx - w / 2, cy - size * 0.02 - w / 2, cx + w / 2, cy - size * 0.02 + w / 2], fill=WHITE)


def badge(d, cx, cy, dia, digit, font):
    d.ellipse([cx - dia / 2, cy - dia / 2, cx + dia / 2, cy + dia / 2], fill=BLUE)
    d.text((cx, cy), digit, font=font, fill=WHITE, anchor="mm")


def draw(fonts, k, logo=None):
    W, H = 640 * k, 420 * k
    img = Image.new("RGB", (W, H), WHITE)
    d = ImageDraw.Draw(img)
    f = lambda face, size: ImageFont.truetype(os.path.join(fonts, "IBMPlexSans-%s.ttf" % face), round(size * k))
    bold, sub, body, foot = f("Bold", 19.5), f("Regular", 13), f("Regular", 13), f("Regular", 11)
    big, small = f("Bold", 13), f("Bold", 11)

    # head: icon, name, subtitle, rule
    icon(d, 24 * k, 20 * k, 28 * k, k)
    d.text((62 * k, 40.5 * k), NAME, font=bold, fill=INK, anchor="ls")
    nx = 62 * k + d.textlength(NAME, font=bold) + 10 * k
    d.text((nx, 40.5 * k), SUBTITLE, font=sub, fill=GRAY, anchor="ls")
    d.rectangle([24 * k, 62 * k, 616 * k - 1, 62 * k + k - 1], fill=RULE)
    # the dmajorsound logo top right: 36 pt high, right edge at 616, top at 16 (level with the icon, above the rule)
    if logo is not None:
        h = 36 * k
        w = round(logo.width * h / logo.height)
        lg = logo.resize((w, h), Image.LANCZOS)
        img.paste(lg, (616 * k - w, 16 * k), lg)

    # over the icons: 1 above Install, 2 above Blocked, a line before Uninstall
    badge(d, 150 * k, 89 * k, 22.5 * k, "1", big)
    badge(d, 330 * k, 89 * k, 22.5 * k, "2", big)
    d.rectangle([420 * k, 90 * k, 420 * k + k - 1, 220 * k], fill=RULE)

    # the three steps
    y = 272
    for i, lines in enumerate(STEPS):
        badge(d, 33 * k, y * k, 18.5 * k, str(i + 1), small)
        for j, line in enumerate(lines):
            d.text((52 * k, (y + 20 * j) * k), line, font=body, fill=INK, anchor="lm")
        y += 20 * len(lines) + (4 if len(lines) > 1 else 0)

    d.text((24 * k, 394 * k), FOOTER, font=foot, fill=GRAY, anchor="lm")
    return img


if __name__ == "__main__":
    fonts, out = sys.argv[1], sys.argv[2]
    here = os.path.dirname(os.path.abspath(__file__))
    logo = Image.open(sys.argv[3] if len(sys.argv) > 3 else os.path.join(here, "..", "installer", "mac_ui", "logo.png")).convert("RGBA")
    os.makedirs(out, exist_ok=True)
    draw(fonts, 1, logo).save(os.path.join(out, "background.png"), optimize=True)
    draw(fonts, 2, logo).save(os.path.join(out, "background@2x.png"), optimize=True)
