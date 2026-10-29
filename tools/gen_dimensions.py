#!/usr/bin/env python3
"""Dimension drawing of the KB Bridge v0.7 PCBA for people who design their own case.

  python tools/gen_dimensions.py            (needs matplotlib)
Writes docs/hardware/kb_bridge_v0.7_dimensions.pdf and docs/img/buttons.svg (the guide's button map).

Frame (same as docs/hardware/kb_bridge_v0.7_pcba.stl): origin = board centre, X right, +Y towards J1
(USB-C receptacle), Z up from the board's bottom face; the component side (J1, J2, buttons, LED) is the
top. Numbers come from the KiCad board (positions) and its STEP export (heights, connector bodies).
"""
import os
import re

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.patches import Arc, Circle, FancyBboxPatch, Rectangle

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "hardware", "kb_bridge_v0.7_dimensions.pdf")
OUT_BUTTONS = os.path.join(ROOT, "docs", "img", "buttons.svg")

W, H, R, T = 56.0, 26.0, 2.0, 1.6             # board, corner radius, thickness
NOTCH_W, NOTCH_D = 10.0, 0.7                  # -Y edge, behind J2
HOLES = [(sx * 25.5, sy * 10.5) for sx in (-1, 1) for sy in (-1, 1)]
HOLE_D = 2.2
J1 = dict(x0=-4.47, x1=4.47, y0=6.40, y1=14.30, z0=0.74, z1=4.85)      # USB-C receptacle (power / PC)
J2 = dict(x0=-4.40, x1=4.40, y0=-24.75, y1=-9.00, z0=0.29, z1=3.20)    # USB-C plug (keyboard)
SW = {"PAIR": (-19.0, -10.6), "LOW-I": (-12.0, -10.6)}                 # TS-1088, body 3.9 x 2.9, top Z 3.54
SW_BODY, SW_TOP = (3.9, 2.9), 3.54
LED = (-14.0, 8.0)                                                     # 0603 bi-colour, top Z 2.70
PWR = {"+5V": (18.0, 10.4), "GND": (20.54, 10.4)}                     # 1.0 mm holes, 2.54 pitch
BOOT = [(1.2, -5.6), (2.8, -5.6)]                                      # JP1, 0.8 mm holes
DIP = dict(x0=-26.28, x1=0.22, y0=-5.69, y1=5.31, z0=-1.09)            # AE-HY0020-DIP board under the PCB
MODULE = dict(x0=-18.93, x1=-8.93, y0=0.89, y1=4.39, z0=-2.14)         # HY0020 module (antenna)
PINS = dict(x0=-25.4, x1=-0.65, y0=-4.95, y1=4.55)                     # DIP pins through the PCB (top side)
TOP_MAX_EX_J1 = 3.54                                                   # buttons; everything else is lower
PINS_TOP = T + 2.0                                                     # cut the DIP pins to 2 mm or less
BOTTOM_KEEP = -2.9                                                     # free space below for the DIP + solder

INK = "#1d1d1b"
DIM = "#2f6f4f"
FILL = "#e9eee9"
PART = "#c9d6cc"
NOTE = "#6b6b66"


def outline(ax, lw=1.4):
    """board outline with the J2 notch, top view"""
    import numpy as np
    pts = []

    def arc(cx, cy, a0, a1):
        for a in np.linspace(np.radians(a0), np.radians(a1), 12):
            pts.append((cx + R * np.cos(a), cy + R * np.sin(a)))

    x, y = W / 2, H / 2
    arc(x - R, y - R, 0, 90)
    arc(-x + R, y - R, 90, 180)
    arc(-x + R, -y + R, 180, 270)
    pts += [(-NOTCH_W / 2, -y), (-NOTCH_W / 2, -y + NOTCH_D), (NOTCH_W / 2, -y + NOTCH_D), (NOTCH_W / 2, -y)]
    arc(x - R, -y + R, 270, 360)
    pts.append(pts[0])
    xs, ys = zip(*pts)
    ax.fill(xs, ys, color=FILL, zorder=0)
    ax.plot(xs, ys, color=INK, lw=lw, zorder=3)


def rect(ax, x0, y0, x1, y1, fc=PART, ec=INK, lw=0.8, ls="-", z=2, label=None, fs=6):
    ax.add_patch(Rectangle((x0, y0), x1 - x0, y1 - y0, fc=fc, ec=ec, lw=lw, ls=ls, zorder=z))
    if label:
        ax.text((x0 + x1) / 2, (y0 + y1) / 2, label, ha="center", va="center", fontsize=fs, color=INK, zorder=z + 1)


def dim(ax, p0, p1, off, text=None, fs=6.5, ext=True):
    """linear dimension between p0 and p1, drawn `off` away (perpendicular, + = left of p0->p1)"""
    import numpy as np
    p0, p1 = np.array(p0, float), np.array(p1, float)
    d = p1 - p0
    n = np.array([-d[1], d[0]]) / np.linalg.norm(d)
    a, b = p0 + n * off, p1 + n * off
    if ext:
        for p, q in ((p0, a), (p1, b)):
            e = q + n * 0.8 * np.sign(off)
            ax.plot([p[0], e[0]], [p[1], e[1]], color=DIM, lw=0.4, zorder=4)
    ax.annotate("", xy=b, xytext=a, arrowprops=dict(arrowstyle="<->", color=DIM, lw=0.6, shrinkA=0, shrinkB=0), zorder=4)
    t = text if text is not None else "%g" % round(float(np.linalg.norm(d)), 2)
    m = (a + b) / 2 + n * 0.9 * (1 if off >= 0 else -1)
    ang = np.degrees(np.arctan2(d[1], d[0]))
    if ang > 90 or ang <= -90:
        ang += 180
    ax.text(m[0], m[1], t, ha="center", va="center", rotation=ang, fontsize=fs, color=DIM, zorder=5,
            bbox=dict(fc="white", ec="none", pad=0.3))


def setup(ax, title, xlim, ylim):
    ax.set_aspect("equal")
    ax.set_xlim(*xlim)
    ax.set_ylim(*ylim)
    ax.axis("off")
    ax.set_title(title, fontsize=10, loc="left", color=INK)
    ax.plot([xlim[0] + 1, xlim[0] + 4], [ylim[0] + 1, ylim[0] + 1], color=NOTE, lw=0.6)
    ax.plot([xlim[0] + 1, xlim[0] + 1], [ylim[0] + 1, ylim[0] + 4], color=NOTE, lw=0.6)


def top_view(ax):
    setup(ax, "Top view (component side)", (-40, 40), (-33, 25))
    ax.text(-38.5, -32.0, "X", fontsize=6, color=NOTE)
    ax.text(-39.6, -28.6, "Y", fontsize=6, color=NOTE)
    outline(ax)
    for x, y in HOLES:
        ax.add_patch(Circle((x, y), HOLE_D / 2, fc="white", ec=INK, lw=0.8, zorder=3))
        ax.plot([x - 1.8, x + 1.8], [y, y], color=NOTE, lw=0.3, zorder=4)
        ax.plot([x, x], [y - 1.8, y + 1.8], color=NOTE, lw=0.3, zorder=4)
    rect(ax, J1["x0"], J1["y0"], J1["x1"], J1["y1"], label="J1\nUSB-C\n(power / PC)")
    rect(ax, J2["x0"], J2["y0"], J2["x1"], J2["y1"], label="J2\nUSB-C plug\n(keyboard)")
    for name, (x, y) in SW.items():
        rect(ax, x - SW_BODY[0] / 2, y - SW_BODY[1] / 2, x + SW_BODY[0] / 2, y + SW_BODY[1] / 2, label=name, fs=5.5)
    ax.add_patch(Circle(LED, 0.9, fc="#f4d35e", ec=INK, lw=0.6, zorder=3))
    ax.text(LED[0] + 1.4, LED[1], "LED", fontsize=6, va="center", color=INK)
    for name, (x, y) in PWR.items():
        ax.add_patch(Circle((x, y), 0.5, fc="white", ec=INK, lw=0.7, zorder=3))
        ax.add_patch(Circle((x, y), 0.85, fc="none", ec=INK, lw=0.5, zorder=3))
        ax.text(x, y - 1.9, name, ha="center", fontsize=5.5, color=INK)
    for x, y in BOOT:
        ax.add_patch(Circle((x, y), 0.4, fc="white", ec=INK, lw=0.6, zorder=3))
    ax.text(2.0, -7.2, "JP1 BOOT", ha="center", fontsize=5, color=NOTE)
    rect(ax, PINS["x0"], PINS["y0"], PINS["x1"], PINS["y1"], fc="none", ec=NOTE, lw=0.6, ls="--", z=2)
    ax.text(-13, -3.6, "HY0020 DIP pins (cut to ≤ 2.0 above the board)", ha="center", fontsize=5.2, color=NOTE)

    # overall and holes
    dim(ax, (-W / 2, H / 2), (W / 2, H / 2), 9)
    dim(ax, (-W / 2, -H / 2), (-W / 2, H / 2), 5.5)
    dim(ax, (-25.5, -10.5), (25.5, -10.5), -19.0, "51 (M2 holes, φ2.2 ×4)")
    dim(ax, (25.5, -10.5), (25.5, 10.5), -6.5, "21")
    # connectors
    dim(ax, (J1["x0"], J1["y1"]), (J1["x1"], J1["y1"]), 2.5, "8.94")
    dim(ax, (J1["x1"], H / 2), (J1["x1"], J1["y1"]), -4.0, "1.3")
    dim(ax, (J2["x1"], -H / 2), (J2["x1"], J2["y0"]), 6.5, "11.75")
    dim(ax, (J2["x0"], J2["y0"]), (J2["x1"], J2["y0"]), 2.5, "8.8")
    # buttons, LED, power pins from the centre lines
    dim(ax, (SW["PAIR"][0], -H / 2), (SW["LOW-I"][0], -H / 2), -4.0, "7.0")
    dim(ax, (0, -H / 2), (SW["LOW-I"][0], -H / 2), 7.5, "12.0")
    ax.text(-15.5, -16.2, "buttons: centre Y −10.6", ha="center", fontsize=5.5, color=DIM)
    ax.text(LED[0], LED[1] + 2.0, "(−14.0, 8.0)", ha="center", fontsize=5.5, color=DIM)
    ax.text(19.27, 14.0, "(18.0, 10.4) / (20.54, 10.4)\n2.54 pitch, φ1.0", ha="center", fontsize=5.2, color=DIM)
    ax.plot([0, 0], [-27, 17], color=NOTE, lw=0.3, ls="-.", zorder=1)
    ax.plot([-31, 31], [0, 0], color=NOTE, lw=0.3, ls="-.", zorder=1)


def side_view(ax):
    setup(ax, "Side view (Y–Z, keyboard side left)", (-30, 20), (-6, 9))
    ax.text(-28.5, -5.1, "Y", fontsize=6, color=NOTE)
    ax.text(-29.6, -1.6, "Z", fontsize=6, color=NOTE)
    # in this view the horizontal axis is Y (left = -Y, keyboard side), vertical = Z
    rect(ax, -H / 2, 0, H / 2, T, fc=FILL, lw=1.2)
    rect(ax, J2["y0"], J2["z0"], J2["y1"], J2["z1"], fc="#e3e8e4", label="J2", fs=6, z=1)
    rect(ax, J1["y0"], J1["z0"], J1["y1"], J1["z1"], label="J1", fs=6)
    rect(ax, -12.05, T, -9.15, SW_TOP, label="SW", fs=5)
    rect(ax, DIP["y0"], DIP["z0"], DIP["y1"], 0, fc="#d8d3c4", label="HY0020 DIP board", fs=5.2)
    rect(ax, MODULE["y0"], MODULE["z0"], MODULE["y1"], DIP["z0"], fc="#cfc6aa", label="module", fs=5)
    ax.plot([-H / 2, H / 2], [BOTTOM_KEEP, BOTTOM_KEEP], color=NOTE, lw=0.6, ls="--")
    ax.text(-H / 2 - 0.5, BOTTOM_KEEP, "keep clear to Z −2.9", fontsize=5.5, va="center", ha="right", color=NOTE)
    ax.plot([-6, 6], [PINS_TOP, PINS_TOP], color=NOTE, lw=0.6, ls="--")
    ax.text(0, PINS_TOP + 0.5, "DIP pin tips ≤ Z 3.6", fontsize=5.5, ha="center", color=NOTE)
    dim(ax, (H / 2 + 2, 0), (H / 2 + 2, T), -1.5, "1.6")
    dim(ax, (J1["y1"], 0), (J1["y1"], J1["z1"]), -2.0, "4.85")
    dim(ax, (-9.15, 0), (-9.15, SW_TOP), -1.5, "3.54")
    dim(ax, (J2["y0"], 0), (J2["y0"], J2["z1"]), 1.5, "3.2")
    dim(ax, (DIP["y1"] + 0.5, 0), (DIP["y1"] + 0.5, MODULE["z0"]), -1.5, "2.14", ext=False)
    ax.text(J2["y0"] + 3, J2["z0"] - 0.9, "plug centre Z 1.75", fontsize=5.5, color=DIM)


def notes(ax):
    ax.axis("off")
    rows = [
        ("Origin", "board centre, X right, +Y towards J1, Z = board bottom (0)"),
        ("STL", "kb_bridge_v0.7_pcba.stl: same frame (mm), HY0020 fitted"),
        ("Board", "56.0 × 26.0 × 1.6, corner R2.0, notch 10 × 0.7 behind J2"),
        ("Mounting", "4 × φ2.2 (M2) at (±25.5, ±10.5)"),
        ("J1 power / PC", "USB-C receptacle, front face Y +14.3 (1.3 out of the edge), Z 0.74–4.85"),
        ("J2 keyboard", "USB-C plug, tip Y −24.75 (11.75 out of the edge), Z 0.29–3.20, centre Z 1.75"),
        ("Top side", "tallest: J1 4.85, buttons 3.54 (TS-1088, pressed from the top), LED 2.70"),
        ("Bottom", "HY0020 DIP board + module down to Z −2.14; keep Z −2.9 free"),
        ("5V / GND", "two φ1.0 holes, 2.54 pitch at (18.0, 10.4) +5V and (20.54, 10.4) GND:\n"
                     "power without J1 (4.5–5.5 V; same input as J1, reverse-protected)"),
        ("LED", "bi-colour (red / green) at (−14.0, 8.0) on the top side: leave a light hole"),
        ("Antenna", "keep metal away from the HY0020 module (bottom side, Y +0.9 … +4.4)"),
    ]
    import textwrap
    y = 1.0
    for k, v in rows:
        lines = [l for part in v.split("\n") for l in textwrap.wrap(part, 52)]
        ax.text(0.0, y, k, fontsize=7, va="top", color=INK, weight="bold", transform=ax.transAxes)
        ax.text(0.0, y - 0.026, "\n".join(lines), fontsize=6.5, va="top", color=INK, linespacing=1.35,
                transform=ax.transAxes)
        y -= 0.05 + 0.024 * len(lines)
    ax.text(0.0, y - 0.02, "All dimensions in mm. KB Bridge v0.7.", fontsize=6, color=NOTE,
            va="top", transform=ax.transAxes)


def buttons_view(ax):
    """guide figure: which button is which, and JP1; the rest only for orientation"""
    ax.set_aspect("equal")
    ax.set_xlim(-31, 31)
    ax.set_ylim(-27.5, 17.5)
    ax.axis("off")
    outline(ax, lw=1.0)
    for x, y in HOLES:
        ax.add_patch(Circle((x, y), HOLE_D / 2, fc="white", ec=NOTE, lw=0.6, zorder=3))
    rect(ax, J1["x0"], J1["y0"], J1["x1"], J1["y1"], fc="white", ec=NOTE, lw=0.6)
    ax.text(0, 10.6, "J1\npower / PC", ha="center", va="center", fontsize=7, color=NOTE)
    rect(ax, J2["x0"], J2["y0"], J2["x1"], J2["y1"], fc="white", ec=NOTE, lw=0.6)
    ax.text(0, -18.5, "J2\nkeyboard", ha="center", va="center", fontsize=7, color=NOTE)
    ax.add_patch(Circle(LED, 0.9, fc="#f4d35e", ec=NOTE, lw=0.5, zorder=3))
    ax.text(LED[0] + 1.6, LED[1], "LED", fontsize=7, va="center", color=NOTE)
    for name, (x, y) in SW.items():
        rect(ax, x - SW_BODY[0] / 2, y - SW_BODY[1] / 2, x + SW_BODY[0] / 2, y + SW_BODY[1] / 2,
             fc=DIM, ec=INK, lw=0.8, z=4)
        ax.add_patch(Circle((x, y), 0.8, fc=INK, ec="none", zorder=5))
        ax.text(x, y + 2.6, name, ha="center", va="bottom", fontsize=10, fontweight="bold", color=INK)
    for x, y in BOOT:
        ax.add_patch(Circle((x, y), 0.6, fc="white", ec=INK, lw=1.0, zorder=5))
    ax.add_patch(Rectangle((0.2, -6.6), 3.6, 2.0, fc="none", ec=DIM, lw=1.2, zorder=4))
    ax.text(2.0, -3.9, "JP1 (BOOT)", ha="center", va="bottom", fontsize=9, fontweight="bold", color=INK)
    ax.text(-30.5, 16.5, "Component side (the side with the J1 USB-C receptacle)", fontsize=7, va="top", color=NOTE)


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    plt.rcParams["font.family"] = ["Hiragino Sans", "Arial Unicode MS", "DejaVu Sans"]
    with PdfPages(OUT) as pdf:
        fig = plt.figure(figsize=(11.69, 8.27))                       # A4 landscape
        fig.suptitle("KB Bridge v0.7 — PCBA dimensions", x=0.04, ha="left", fontsize=13, color=INK)
        top_view(fig.add_axes([0.02, 0.33, 0.60, 0.59]))
        side_view(fig.add_axes([0.02, 0.03, 0.60, 0.26]))
        notes(fig.add_axes([0.64, 0.06, 0.34, 0.84]))
        pdf.savefig(fig)
        plt.close(fig)
    print("wrote", OUT)
    plt.rcParams["svg.fonttype"] = "none"                              # text stays text (browser fonts)
    fig = plt.figure(figsize=(6.2, 4.5))
    buttons_view(fig.add_axes([0, 0, 1, 1]))
    fig.savefig(OUT_BUTTONS, transparent=True)
    with open(OUT_BUTTONS) as f:                                       # any sans-serif where these are missing
        svg = f.read().replace("'DejaVu Sans'", "'DejaVu Sans', sans-serif")
    svg = re.sub(r'<svg([^>]*?) width="[^"]*" height="[^"]*"', r'<svg\1 width="100%"', svg, count=1)   # scales to its box
    with open(OUT_BUTTONS, "w") as f:
        f.write(svg)
    plt.close(fig)
    print("wrote", OUT_BUTTONS)


if __name__ == "__main__":
    main()
