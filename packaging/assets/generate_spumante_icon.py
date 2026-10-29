#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build the Spumante app icon (.icns) from the official repo logo.

The artwork is `docs/assets/spumante-logo.png` — the same glass used in the README and the menu
bar tray. It is centred on a standard macOS Big Sur-style rounded tile so it looks native in the
Dock, in Finder and on the mounted disk image.

This is an optional design-time tool: the generated `packaging/assets/spumante.icns` and
`packaging/assets/spumante-icon-1024.png` are committed, so normal builds never need Pillow.

Usage:
    python3 packaging/assets/generate_spumante_icon.py

Requires Pillow (`python3 -m pip install pillow`) and macOS `iconutil` for the .icns step.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError:  # pragma: no cover - design-time tool only
    sys.exit("This tool needs Pillow: python3 -m pip install pillow")

ASSET_DIR = Path(__file__).resolve().parent
REPO_ROOT = ASSET_DIR.parents[1]
LOGO_PATH = REPO_ROOT / "docs" / "assets" / "spumante-logo.png"

SIZE = 1024
SS = 4  # supersampling factor
S = SIZE * SS

# Big Sur style: the tile occupies most of the canvas with a small transparent margin.
INSET = 84
RADIUS = 205
ARTWORK_SIZE = 780  # logo is drawn inside the tile at this size (source is 512 px)

TILE_TOP = (255, 255, 255)
TILE_BOTTOM = (244, 241, 235)


def _vertical_gradient(size: tuple[int, int], top, bottom) -> Image.Image:
    w, h = size
    grad = Image.new("RGB", (1, h))
    px = grad.load()
    for y in range(h):
        t = y / max(1, h - 1)
        px[0, y] = tuple(int(round(top[i] + (bottom[i] - top[i]) * t)) for i in range(3))
    return grad.resize((w, h), Image.BILINEAR)


def build_icon() -> Image.Image:
    logo = Image.open(LOGO_PATH).convert("RGBA")

    tile_mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(tile_mask).rounded_rectangle(
        (INSET * SS, INSET * SS, S - INSET * SS, S - INSET * SS), radius=RADIUS * SS, fill=255
    )

    canvas = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    tile = _vertical_gradient((S, S), TILE_TOP, TILE_BOTTOM).convert("RGBA")
    canvas.paste(tile, (0, 0), tile_mask)

    # Soft hairline so the white tile keeps its edge on light backgrounds.
    edge = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(edge).rounded_rectangle(
        (INSET * SS, INSET * SS, S - INSET * SS, S - INSET * SS),
        radius=RADIUS * SS,
        outline=(0, 0, 0, 26),
        width=2 * SS,
    )
    canvas.alpha_composite(Image.composite(edge, Image.new("RGBA", (S, S), (0, 0, 0, 0)), tile_mask))

    artwork = logo.resize((ARTWORK_SIZE * SS, ARTWORK_SIZE * SS), Image.LANCZOS)
    offset = (S - artwork.width) // 2
    canvas.alpha_composite(artwork, (offset, offset))

    return canvas.resize((SIZE, SIZE), Image.LANCZOS)


def main() -> int:
    if not LOGO_PATH.is_file():
        sys.exit(f"Missing source artwork: {LOGO_PATH}")
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
            icon.resize((px, px), Image.LANCZOS).save(iconset / f"icon_{base}x{base}{suffix}.png")

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
