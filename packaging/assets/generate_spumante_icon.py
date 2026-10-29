#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate the Spumante prosecco-flute application icon (PNG + .icns).

This is an optional design-time tool: the generated assets (`packaging/assets/spumante-icon-1024.png`
and `packaging/assets/spumante.icns`) are committed to the repository, so normal builds never need
Pillow or any other image library.

Usage:
    python3 packaging/assets/generate_spumante_icon.py

Requires Pillow (`python3 -m pip install pillow`) and macOS `iconutil` for the .icns step.
"""

from __future__ import annotations

import math
import shutil
import subprocess
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFilter
except ImportError:  # pragma: no cover - design-time tool only
    sys.exit("This tool needs Pillow: python3 -m pip install pillow")

ASSET_DIR = Path(__file__).resolve().parent
SIZE = 1024
SS = 4  # supersampling factor
S = SIZE * SS

# Palette: prosecco bottle glass (background) + sparkling golden wine (flute).
BG_TOP = (24, 92, 72)
BG_BOTTOM = (6, 26, 21)
GLASS_EDGE = (232, 248, 242)
GLASS_FILL = (255, 255, 255)
WINE_TOP = (255, 240, 186)
WINE_MID = (247, 200, 96)
WINE_BOTTOM = (214, 141, 18)
GOLD = (255, 214, 102)


def _lerp(a: tuple[int, int, int], b: tuple[int, int, int], t: float) -> tuple[int, int, int]:
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def _vertical_gradient(size: tuple[int, int], top: tuple[int, int, int], bottom: tuple[int, int, int]) -> Image.Image:
    w, h = size
    grad = Image.new("RGB", (1, h))
    px = grad.load()
    for y in range(h):
        px[0, y] = _lerp(top, bottom, y / max(1, h - 1))
    return grad.resize((w, h), Image.BILINEAR)


def _squircle_mask(size: int, inset: int, radius: int) -> Image.Image:
    mask = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(mask)
    draw.rounded_rectangle((inset, inset, size - inset, size - inset), radius=radius, fill=255)
    return mask


def _bezier(p0, p1, p2, p3, steps: int = 64) -> list[tuple[float, float]]:
    points = []
    for i in range(steps + 1):
        t = i / steps
        mt = 1 - t
        x = mt**3 * p0[0] + 3 * mt**2 * t * p1[0] + 3 * mt * t**2 * p2[0] + t**3 * p3[0]
        y = mt**3 * p0[1] + 3 * mt**2 * t * p1[1] + 3 * mt * t**2 * p2[1] + t**3 * p3[1]
        points.append((x, y))
    return points


def _bowl_outline() -> list[tuple[float, float]]:
    """Tulip-shaped prosecco bowl (supersampled coordinates)."""
    def sc(x: float, y: float) -> tuple[float, float]:
        return (x * SS, y * SS)

    rim_left = (414, 250)
    rim_right = (610, 250)
    foot_left = (478, 642)
    foot_right = (546, 642)

    left = _bezier(sc(*rim_left), sc(408, 420), sc(444, 560), sc(*foot_left))
    bottom = _bezier(sc(*foot_left), sc(488, 668), sc(536, 668), sc(*foot_right))
    right = _bezier(sc(*foot_right), sc(580, 560), sc(616, 420), sc(*rim_right))
    return left + bottom + right


def _liquid_mask(points: list[tuple[float, float]], level_y: float) -> Image.Image:
    mask = Image.new("L", (S, S), 0)
    draw = ImageDraw.Draw(mask)
    draw.polygon(points, fill=255)
    # Cut everything above the wine level (keeps the surface flat).
    cut = Image.new("L", (S, S), 0)
    ImageDraw.Draw(cut).rectangle((0, 0, S, level_y * SS), fill=255)
    mask = Image.composite(Image.new("L", (S, S), 0), mask, cut)
    return mask


def _draw_bubbles(draw: ImageDraw.ImageDraw, bubbles: list[tuple[float, float, float, int]]) -> None:
    for cx, cy, r, alpha in bubbles:
        draw.ellipse(
            ((cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy + r) * SS),
            fill=(255, 247, 214, alpha),
        )


def _draw_sparkle(draw: ImageDraw.ImageDraw, cx: float, cy: float, size: float, alpha: int) -> None:
    draw.polygon(
        [
            (cx * SS, (cy - size) * SS),
            ((cx + size * 0.24) * SS, (cy - size * 0.24) * SS),
            ((cx + size) * SS, cy * SS),
            ((cx + size * 0.24) * SS, (cy + size * 0.24) * SS),
            (cx * SS, (cy + size) * SS),
            ((cx - size * 0.24) * SS, (cy + size * 0.24) * SS),
            ((cx - size) * SS, cy * SS),
            ((cx - size * 0.24) * SS, (cy - size * 0.24) * SS),
        ],
        fill=(255, 246, 200, alpha),
    )


def build_icon() -> Image.Image:
    canvas = Image.new("RGBA", (S, S), (0, 0, 0, 0))

    # --- Background: rounded macOS icon field with a soft glow -----------------
    bg_mask = _squircle_mask(S, int(46 * SS), int(232 * SS))
    bg = _vertical_gradient((S, S), BG_TOP, BG_BOTTOM).convert("RGBA")

    glow = Image.new("L", (S, S), 0)
    ImageDraw.Draw(glow).ellipse(
        (int(150 * SS), int(-260 * SS), int(900 * SS), int(520 * SS)), fill=90
    )
    glow = glow.filter(ImageFilter.GaussianBlur(int(90 * SS)))
    bg = Image.composite(Image.new("RGBA", (S, S), (140, 235, 205, 255)), bg, glow.point(lambda v: v))
    canvas.paste(bg, (0, 0), bg_mask)

    # Inner rim highlight so the icon reads as a physical tile.
    rim = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(rim).rounded_rectangle(
        (int(58 * SS), int(58 * SS), S - int(58 * SS), S - int(58 * SS)),
        radius=int(220 * SS),
        outline=(190, 255, 235, 70),
        width=int(4 * SS),
    )
    canvas.alpha_composite(rim)

    bowl = _bowl_outline()
    bowl_mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(bowl_mask).polygon(bowl, fill=255)

    # --- Wine inside the bowl --------------------------------------------------
    wine_level = 366
    wine_mask = _liquid_mask(bowl, wine_level)
    wine = _vertical_gradient((S, S), WINE_TOP, WINE_BOTTOM).convert("RGBA")
    canvas.paste(wine, (0, 0), wine_mask)

    # Wine surface meniscus (a soft ellipse so the level reads as liquid, not a cut).
    surface = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sdraw = ImageDraw.Draw(surface)
    sdraw.ellipse(
        (int(414 * SS), int((wine_level - 12) * SS), int(610 * SS), int((wine_level + 14) * SS)),
        fill=(255, 250, 224, 110),
    )
    canvas.alpha_composite(Image.composite(surface, Image.new("RGBA", (S, S), (0, 0, 0, 0)), wine_mask))

    # --- Stem and foot ---------------------------------------------------------
    stem = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sdraw = ImageDraw.Draw(stem)
    sdraw.polygon(
        [
            (int(500 * SS), int(660 * SS)),
            (int(524 * SS), int(660 * SS)),
            (int(521 * SS), int(812 * SS)),
            (int(503 * SS), int(812 * SS)),
        ],
        fill=(*GLASS_FILL, 40),
        outline=(*GLASS_EDGE, 230),
        width=int(8 * SS),
    )
    sdraw.ellipse(
        (int(372 * SS), int(806 * SS), int(652 * SS), int(876 * SS)),
        fill=(*GLASS_FILL, 40),
        outline=(*GLASS_EDGE, 235),
        width=int(9 * SS),
    )
    sdraw.ellipse(
        (int(396 * SS), int(822 * SS), int(628 * SS), int(860 * SS)),
        fill=(255, 255, 255, 22),
    )
    canvas.alpha_composite(stem)

    # --- Glass body ------------------------------------------------------------
    glass = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    gdraw = ImageDraw.Draw(glass)
    gdraw.polygon(bowl, fill=(*GLASS_FILL, 26))
    gdraw.line([tuple(p) for p in bowl], fill=(*GLASS_EDGE, 235), width=int(9 * SS), joint="curve")
    # Fill the closing rim and the bowl cut edge.
    gdraw.line(
        [(int(414 * SS), int(250 * SS)), (int(610 * SS), int(250 * SS))],
        fill=(*GLASS_EDGE, 235),
        width=int(9 * SS),
    )
    # Rim mouth ellipse.
    gdraw.ellipse(
        (int(414 * SS), int(232 * SS), int(610 * SS), int(272 * SS)),
        outline=(*GLASS_EDGE, 240),
        width=int(8 * SS),
    )
    gdraw.ellipse(
        (int(424 * SS), int(238 * SS), int(600 * SS), int(266 * SS)),
        fill=(255, 255, 255, 34),
    )
    canvas.alpha_composite(glass)

    # --- Sparkling perlage -----------------------------------------------------
    perlage = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    pdraw = ImageDraw.Draw(perlage)
    bubbles = [
        (470, 520, 7, 170), (505, 555, 5, 140), (545, 500, 6, 150),
        (492, 458, 8, 165), (533, 545, 4, 120), (575, 545, 5, 130),
        (452, 476, 4, 110), (512, 418, 6, 150), (566, 470, 7, 140),
        (486, 575, 4, 100), (520, 590, 3, 90),
    ]
    _draw_bubbles(pdraw, bubbles)
    # Bubbles rising above the rim.
    _draw_bubbles(
        pdraw,
        [(700, 210, 9, 120), (728, 158, 6, 105), (686, 120, 5, 90), (752, 96, 4, 70),
         (612, 196, 5, 80)],
    )
    _draw_sparkle(pdraw, 690, 250, 26, 170)
    _draw_sparkle(pdraw, 772, 196, 18, 130)
    _draw_sparkle(pdraw, 640, 132, 14, 110)
    canvas.alpha_composite(perlage)

    # --- Glass highlights ------------------------------------------------------
    shine = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    hdraw = ImageDraw.Draw(shine)
    hdraw.line(
        [(int(456 * SS), int(300 * SS)), (int(470 * SS), int(352 * SS))],
        fill=(255, 255, 255, 120),
        width=int(12 * SS),
    )
    shine = shine.filter(ImageFilter.GaussianBlur(int(3 * SS)))
    canvas.alpha_composite(Image.composite(shine, Image.new("RGBA", (S, S), (0, 0, 0, 0)), bowl_mask))


    return canvas.resize((SIZE, SIZE), Image.LANCZOS)


def main() -> int:
    icon = build_icon()
    png_path = ASSET_DIR / "spumante-icon-1024.png"
    icon.save(png_path)
    print(f"Wrote {png_path}")

    iconset = ASSET_DIR / "spumante.iconset"
    if iconset.exists():
        shutil.rmtree(iconset)
    iconset.mkdir()
    for base in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            px = base * scale
            suffix = "" if scale == 1 else "@2x"
            name = f"icon_{base}x{base}{suffix}.png"
            icon.resize((px, px), Image.LANCZOS).save(iconset / name)

    iconutil = shutil.which("iconutil")
    if not iconutil:
        print("iconutil not found; iconset left in place (macOS required for .icns)")
        return 0
    icns_path = ASSET_DIR / "spumante.icns"
    subprocess.run([iconutil, "-c", "icns", str(iconset), "-o", str(icns_path)], check=True)
    shutil.rmtree(iconset)
    print(f"Wrote {icns_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
