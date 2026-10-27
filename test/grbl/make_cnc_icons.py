"""Generate the CNC icons (95x95, 24-bit BMP) for the grbl TFT firmware.

The CNC icons reuse the slots of 3D printer icons (see TFT/src/User/Menu/Cnc.h), so the
files are named after those slots. Usage: python make_cnc_icons.py <bmp output folder> [preview.png]
"""
import math
import sys

from PIL import Image, ImageDraw, ImageFont

SIZE = 95
SS = 4  # supersampling factor
S = SIZE * SS
FONT = r"C:\Windows\Fonts\segoeuib.ttf"
WHITE = (255, 255, 255)

TEAL = (0, 150, 136)
BLUE = (33, 150, 243)
ORANGE = (255, 152, 0)
PURPLE = (156, 39, 176)
INDIGO = (63, 81, 181)
GREEN = (76, 175, 80)
RED = (244, 67, 54)
DEEP_ORANGE = (255, 87, 34)


def font(px):
    return ImageFont.truetype(FONT, px * SS)


def canvas(bg):
    im = Image.new("RGB", (S, S), bg)
    return im, ImageDraw.Draw(im)


def text(d, xy, s, px, anchor="mm", fill=WHITE):
    d.text((xy[0] * SS, xy[1] * SS), s, font=font(px), fill=fill, anchor=anchor)


def line(d, pts, w, fill=WHITE):
    d.line([(x * SS, y * SS) for x, y in pts], fill=fill, width=w * SS, joint="curve")


def circle(d, c, r, w=None, fill=None, outline=WHITE):
    box = [(c[0] - r) * SS, (c[1] - r) * SS, (c[0] + r) * SS, (c[1] + r) * SS]
    if w:
        d.ellipse(box, outline=outline, width=w * SS)
    else:
        d.ellipse(box, fill=fill or WHITE)


def poly(d, pts, fill=WHITE):
    d.polygon([(x * SS, y * SS) for x, y in pts], fill=fill)


def crosshair(d, c, r):
    circle(d, c, r, w=4)
    line(d, [(c[0] - r - 6, c[1]), (c[0] + r + 6, c[1])], 4)
    line(d, [(c[0], c[1] - r - 6), (c[0], c[1] + r + 6)], 4)


def zero_icon(axes):
    def draw(d):
        crosshair(d, (47, 38), 19)
        text(d, (47, 80), axes + "=0", 20 if len(axes) < 3 else 17)
    return TEAL, draw


def goto_zero(d):
    crosshair(d, (60, 58), 16)
    line(d, [(14, 18), (44, 44)], 6)
    poly(d, [(50, 50), (34, 46), (46, 34)])
    text(d, (26, 76), "XY", 16)


def ov_icon(letter, value):
    def draw(d):
        text(d, (47, 34), letter, 36)
        text(d, (47, 74), value, 20)
    return draw


def probe(d):
    # router bit above a touch plate, arrow down
    poly(d, [(38, 8), (56, 8), (56, 40), (51, 54), (43, 54), (38, 40)])
    line(d, [(43, 24), (51, 20)], 3, fill=INDIGO)
    line(d, [(43, 34), (51, 30)], 3, fill=INDIGO)
    poly(d, [(68, 44), (84, 44), (76, 58)])
    line(d, [(76, 24), (76, 46)], 5)
    d.rectangle([12 * SS, 64 * SS, 83 * SS, 74 * SS], fill=WHITE)
    text(d, (47, 86), "Z", 14)


def unlock(d):
    d.rounded_rectangle([22 * SS, 44 * SS, 72 * SS, 82 * SS], radius=6 * SS, fill=WHITE)
    # open shackle
    d.arc([28 * SS, 12 * SS, 60 * SS, 52 * SS], start=180, end=360, fill=WHITE, width=7 * SS)
    line(d, [(31, 31), (31, 44)], 7)
    circle(d, (47, 60), 6, fill=GREEN)
    d.rectangle([45 * SS, 60 * SS, 49 * SS, 72 * SS], fill=GREEN)


def spindle_body(d, color):
    # motor body, collet and bit
    d.rounded_rectangle([30 * SS, 8 * SS, 64 * SS, 44 * SS], radius=5 * SS, fill=WHITE)
    for x in (38, 47, 56):
        line(d, [(x, 14), (x, 38)], 3, fill=color)
    poly(d, [(36, 44), (58, 44), (54, 54), (40, 54)])
    poly(d, [(44, 54), (50, 54), (50, 80), (47, 87), (44, 80)])
    line(d, [(44, 62), (50, 58)], 2, fill=color)
    line(d, [(44, 70), (50, 66)], 2, fill=color)


def spindle(d):
    spindle_body(d, DEEP_ORANGE)
    d.arc([18 * SS, 50 * SS, 76 * SS, 92 * SS], start=200, end=340, fill=WHITE, width=4 * SS)
    poly(d, [(72, 58), (80, 66), (70, 68)])


def spindle_on(d):
    spindle_body(d, GREEN)
    text(d, (78, 72), "ON", 13, anchor="mm")


def spindle_off(d):
    spindle_body(d, RED)
    text(d, (76, 72), "OFF", 12, anchor="mm")


def reset(d):
    c, r = (47, 40), 22
    d.arc([(c[0] - r) * SS, (c[1] - r) * SS, (c[0] + r) * SS, (c[1] + r) * SS], start=-60, end=250,
          fill=WHITE, width=7 * SS)
    a = math.radians(-60)
    tip = (c[0] + r * math.cos(a), c[1] + r * math.sin(a))
    poly(d, [(tip[0] - 9, tip[1] - 12), (tip[0] + 10, tip[1] - 4), (tip[0] - 4, tip[1] + 10)])
    text(d, (47, 81), "RESET", 15)


ICONS = {
    "e_1_mm": zero_icon("X"),
    "e_5_mm": zero_icon("Y"),
    "e_10_mm": zero_icon("Z"),
    "e_100_mm": zero_icon("XYZ"),
    "e_200_mm": zero_icon("XY"),
    "e_1_percent": (BLUE, goto_zero),
    "e_5_percent": (ORANGE, ov_icon("F", "-10%")),
    "e_10_percent": (ORANGE, ov_icon("F", "+10%")),
    "1_degree": (PURPLE, ov_icon("S", "100%")),
    "5_degree": (PURPLE, ov_icon("S", "-10%")),
    "10_degree": (PURPLE, ov_icon("S", "+10%")),
    "bltouch_deploy": (INDIGO, probe),
    "bltouch_stow": (GREEN, unlock),
    "bltouch_test": (DEEP_ORANGE, spindle),
    "bltouch_repeat": (GREEN, spindle_on),
    "bltouch_reset": (RED, spindle_off),
    "bltouch": (RED, reset),
}


def main():
    out = sys.argv[1]
    images = []

    for name, (bg, draw) in ICONS.items():
        im, d = canvas(bg)
        draw(d)
        im = im.resize((SIZE, SIZE), Image.LANCZOS)
        im.save(f"{out}/{name}.bmp")
        images.append((name, im))

    if len(sys.argv) > 2:
        sheet = Image.new("RGB", (len(images) * 100, 115), "white")
        sd = ImageDraw.Draw(sheet)
        for i, (name, im) in enumerate(images):
            sheet.paste(im, (i * 100 + 2, 0))
            sd.text((i * 100 + 2, 98), name, fill="black")
        sheet.save(sys.argv[2])

    print(f"{len(images)} icons written to {out}")


if __name__ == "__main__":
    main()
