"""Draw the LiveShell tile icon (icon0.png, 512x512): white line art on black.

Two overlapping rounded screens (the home screen in front, the video layer behind it) and a
solid play symbol. Usage: python make_icon.py <out.png>   Drawn at 4x, downscaled for smooth edges.
"""
import sys

from PIL import Image, ImageDraw

S = 2048
OUT = 512
W = int(S * 0.058)          # outline thickness
R = int(S * 0.085)          # corner radius
GAP = int(S * 0.045)        # black gap between the two screens
WHITE, BLACK = 255, 0


def main(out_path):
    img = Image.new("L", (S, S), BLACK)
    d = ImageDraw.Draw(img)

    back = (S * 0.33, S * 0.17, S * 0.85, S * 0.62)
    front = (S * 0.15, S * 0.36, S * 0.67, S * 0.81)

    d.rounded_rectangle(back, R, outline=WHITE, width=W)
    # Knock out the back screen where the front one sits, plus a gap, like stacked windows.
    d.rounded_rectangle((front[0] - GAP, front[1] - GAP, front[2] + GAP, front[3] + GAP), R + GAP, fill=BLACK)
    d.rounded_rectangle(front, R, outline=WHITE, width=W)

    # Solid play symbol with rounded corners, optically centred in the front screen.
    cx = (front[0] + front[2]) / 2 + S * 0.018
    cy = (front[1] + front[3]) / 2
    h = S * 0.105
    tri = [(cx - h * 0.78, cy - h), (cx - h * 0.78, cy + h), (cx + h * 0.95, cy)]
    d.polygon(tri, fill=WHITE)
    d.line(tri + [tri[0]], fill=WHITE, width=int(S * 0.03), joint="curve")
    rc = S * 0.015
    for x, y in tri:
        d.ellipse((x - rc, y - rc, x + rc, y + rc), fill=WHITE)

    img.resize((OUT, OUT), Image.LANCZOS).convert("RGB").save(out_path, optimize=True)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "icon0.png")
