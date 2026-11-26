#!/usr/bin/env python3
"""Draws Alfred's installer artwork in the style of Ahh 1.1.1 (github.com/dmajorsound/Ahhh, installer/).

    make_branding.py <folder with IBMPlexSans-Regular/Bold.ttf> [repo root]

Writes
  installer/mac_ui/icon.png                       Alfred icon, 256 px (Mac welcome pages, 96 px there)
  installer/mac_dmg/background.png, @2x.png       disk image window, 640 x 420 / 1280 x 840
  installer/win/welcome-icon-100/200.bmp          icon on the Windows welcome page, 72 / 144 px
  installer/win/welcome-brand-100/200.bmp         dmajorsound logo, top right of the welcome page, 44 x 37 / 88 x 75
  installer/win/small-100/200.bmp                 icon top right of the following pages, 55 x 58 / 110 x 116
  installer/win/wizard-100/200.bmp                left picture of the last page: icon in the middle, 164 x 314 / 328 x 628
The dmajorsound logo comes from installer/mac_ui/logo.png.
"""
import os, sys
from PIL import Image, ImageDraw, ImageFont

ROOT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
PLEX = sys.argv[1]
WORD = os.path.join(ROOT, "resources", "fonts", "LiberationSans-Bold.ttf")   # the plug-in's own type

NAME = "Alfred"
SUBTITLE = "A simple Denoiser at your Service"
TILE, EDGE, WHITE, BLUE, INK, GRAY, RULE = (32, 33, 37), (58, 59, 65), (255, 255, 255), (76, 147, 220), (30, 30, 30), (90, 90, 90), (226, 226, 226)
SIGNAL = (76, 147, 220)        # the blue of Alfred's Signal fader and power button


def bowtie(d, cx, cy, w, h, col):
    k = h * 0.18
    d.polygon([(cx - k * 0.9, cy - k * 0.7), (cx - w / 2, cy - h / 2), (cx - w / 2, cy + h / 2), (cx - k * 0.9, cy + k * 0.7)], fill=col)
    d.polygon([(cx + k * 0.9, cy - k * 0.7), (cx + w / 2, cy - h / 2), (cx + w / 2, cy + h / 2), (cx + k * 0.9, cy + k * 0.7)], fill=col)
    d.rounded_rectangle([cx - k * 1.05, cy - k * 1.05, cx + k * 1.05, cy + k * 1.05], radius=k * 0.35, fill=col)


def icon(size=1024):
    """Alfred's icon, in the family of Ahh: a dark rounded tile with the name in white and, above it, a white bow
    tie with a blue dot in the knot - the butler, "at your Service"."""
    s = size
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    r = round(s * 0.215)
    d.rounded_rectangle([0, 0, s - 1, s - 1], radius=r, fill=EDGE)
    d.rounded_rectangle([round(s * 0.008), round(s * 0.008), s - 1 - round(s * 0.008), s - 1 - round(s * 0.008)], radius=r, fill=TILE)
    bowtie(d, s / 2, s * 0.34, s * 0.30, s * 0.16, WHITE)
    dot = s * 0.016
    d.ellipse([s / 2 - dot, s * 0.34 - dot, s / 2 + dot, s * 0.34 + dot], fill=SIGNAL)
    f = ImageFont.truetype(WORD, round(s * 0.235))
    tw = d.textlength(NAME, font=f)
    d.text(((s - tw) / 2, s * 0.60), NAME, font=f, fill=WHITE, anchor="lm")
    return im


def on_white(img):
    bg = Image.new("RGB", img.size, WHITE)
    bg.paste(img, (0, 0), img)
    return bg


def logo():
    return Image.open(os.path.join(ROOT, "installer", "mac_ui", "logo.png")).convert("RGBA")


def fit(img, w, h):
    return img.resize((w, h), Image.LANCZOS)


def dmg(k, ic, lg):
    W, H = 640 * k, 420 * k
    img = Image.new("RGB", (W, H), WHITE)
    d = ImageDraw.Draw(img)
    f = lambda face, size: ImageFont.truetype(os.path.join(PLEX, "IBMPlexSans-%s.ttf" % face), round(size * k))
    bold, sub, body, foot, big, small = f("Bold", 19.5), f("Regular", 13), f("Regular", 13), f("Regular", 11), f("Bold", 13), f("Bold", 11)
    i = fit(ic, 28 * k, 28 * k)
    img.paste(i, (24 * k, 20 * k), i)
    d.text((62 * k, 34 * k), NAME, font=bold, fill=INK, anchor="ls")
    nx = 62 * k + d.textlength(NAME, font=bold) + 10 * k
    d.text((nx, 38 * k), SUBTITLE, font=sub, fill=GRAY, anchor="ls")
    h = 36 * k; w = round(lg.width * h / lg.height); l = fit(lg, w, h)
    img.paste(l, (616 * k - w, 16 * k), l)
    d.rectangle([24 * k, 62 * k, 616 * k - 1, 62 * k + k - 1], fill=RULE)

    def badge(cx, cy, dia, digit, font):
        d.ellipse([cx - dia / 2, cy - dia / 2, cx + dia / 2, cy + dia / 2], fill=BLUE)
        d.text((cx, cy), digit, font=font, fill=WHITE, anchor="mm")
    badge(150 * k, 89 * k, 22.5 * k, "1", big)
    badge(330 * k, 89 * k, 22.5 * k, "2", big)
    d.rectangle([420 * k, 90 * k, 420 * k + k - 1, 220 * k], fill=RULE)
    steps = [["Double-click Install %s and click through." % NAME],
             ["macOS says it can’t be opened? Click Done, then double-click",
              "Blocked? Open Privacy & Security, scroll down and click Open Anyway."],
             ["Double-click Install %s again and choose Open." % NAME]]
    y = 272
    for n, lines in enumerate(steps):
        badge(33 * k, y * k, 18.5 * k, str(n + 1), small)
        for j, line in enumerate(lines):
            d.text((52 * k, (y + 20 * j) * k), line, font=body, fill=INK, anchor="lm")
        y += 20 * len(lines) + (4 if len(lines) > 1 else 0)
    d.text((24 * k, 394 * k), "Quit your DAW first. The installer is free and not signed by Apple, so macOS asks once.", font=foot, fill=GRAY, anchor="lm")
    return img


def boxed(ic, W, H, size, x, y):
    img = Image.new("RGB", (W, H), WHITE)
    i = fit(ic, size, size)
    img.paste(i, (x, y), i)
    return img


if __name__ == "__main__":
    ic, lg = icon(), logo()
    mu, md, mw = (os.path.join(ROOT, "installer", p) for p in ("mac_ui", "mac_dmg", "win"))
    for p in (mu, md, mw): os.makedirs(p, exist_ok=True)
    fit(ic, 256, 256).save(os.path.join(mu, "icon.png"), optimize=True)
    dmg(1, ic, lg).save(os.path.join(md, "background.png"), optimize=True)
    dmg(2, ic, lg).save(os.path.join(md, "background@2x.png"), optimize=True)
    for k in (1, 2):
        sfx = "-%d.bmp" % (100 * k)
        on_white(fit(ic, 72 * k, 72 * k)).save(os.path.join(mw, "welcome-icon" + sfx))
        on_white(fit(lg, 44 * k, 37 if k == 1 else 75)).save(os.path.join(mw, "welcome-brand" + sfx))
        boxed(ic, 55 * k, 58 * k, 46 * k, 4 * k, 6 * k).save(os.path.join(mw, "small" + sfx))
        boxed(ic, 164 * k, 314 * k, 84 * k, 40 * k, 115 * k).save(os.path.join(mw, "wizard" + sfx))
