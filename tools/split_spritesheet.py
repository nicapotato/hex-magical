#!/usr/bin/env python3
"""
Split a spritesheet into individual PNG sprites.

Two modes:
  auto  - Detect irregular sprites on a solid (or transparent) background via
          connected components + nearby fragment merging. Best for sheets like
          nature-sprites.png where sizes vary.
  grid  - Slice a uniform grid (tile size / columns / margins / spacing).

Examples:
  # Auto-detect (nature sheet -> isolated/)
  python3 tools/split_spritesheet.py \\
      resources/spritesheet/nature-sprites.png \\
      -o resources/spritesheet/isolated \\
      --prefix nature

  # Preview bounding boxes without writing sprites
  python3 tools/split_spritesheet.py sheet.png -o /tmp/out --preview /tmp/boxes.png

  # Uniform 16x16 grid, 1px spacing, 1px margin
  python3 tools/split_spritesheet.py sheet.png -o out --mode grid \\
      --tile 16 --spacing 1 --margin 1 --prefix tile

  # 4 columns of 32x48 sprites
  python3 tools/split_spritesheet.py sheet.png -o out --mode grid \\
      --tile 32x48 --columns 4 --prefix hero
"""

from __future__ import annotations

import argparse
import math
import sys
from collections import deque
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError:
    print("Pillow is required: pip install Pillow", file=sys.stderr)
    sys.exit(1)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def parse_size(value: str) -> tuple[int, int]:
    """Parse '16' or '16x32' into (w, h)."""
    if "x" in value.lower():
        w_str, h_str = value.lower().split("x", 1)
        return int(w_str), int(h_str)
    n = int(value)
    return n, n


def is_foreground(pixel: tuple[int, ...], bg_rgb: tuple[int, int, int], thresh: int) -> bool:
    r, g, b, a = pixel
    if a < 10:
        return False
    br, bg, bb = bg_rgb
    return abs(r - br) > thresh or abs(g - bg) > thresh or abs(b - bb) > thresh


def box_gap(a: tuple[int, int, int, int], b: tuple[int, int, int, int]) -> float:
    """Minimum empty-pixel gap between two inclusive AABBs (0 if touching/overlapping)."""
    ax0, ay0, ax1, ay1 = a
    bx0, by0, bx1, by1 = b
    dx = max(0, bx0 - ax1 - 1, ax0 - bx1 - 1)
    dy = max(0, by0 - ay1 - 1, ay0 - by1 - 1)
    if dx == 0 and dy == 0:
        if ax1 < bx0 or bx1 < ax0 or ay1 < by0 or by1 < ay0:
            return math.hypot(
                max(0, bx0 - ax1 - 1, ax0 - bx1 - 1),
                max(0, by0 - ay1 - 1, ay0 - by1 - 1),
            )
        return 0.0
    if dx == 0:
        return float(dy)
    if dy == 0:
        return float(dx)
    return math.hypot(dx, dy)


def reading_order(
    boxes: list[tuple[int, int, int, int]],
    row_tol: float,
) -> list[int]:
    """Return indices sorted top-to-bottom, left-to-right (row clustered by y-center)."""
    items = []
    for i, (x0, y0, x1, y1) in enumerate(boxes):
        items.append(((y0 + y1) / 2.0, (x0 + x1) / 2.0, i))

    rows: list[list[tuple[float, float, int]]] = []
    for cy, cx, i in sorted(items, key=lambda t: (t[0], t[1])):
        placed = False
        for row in rows:
            mean_y = sum(t[0] for t in row) / len(row)
            if abs(cy - mean_y) <= row_tol:
                row.append((cy, cx, i))
                placed = True
                break
        if not placed:
            rows.append([(cy, cx, i)])

    ordered: list[int] = []
    for row in rows:
        for _, _, i in sorted(row, key=lambda t: t[1]):
            ordered.append(i)
    return ordered


# ---------------------------------------------------------------------------
# Auto mode
# ---------------------------------------------------------------------------

def detect_auto(
    image: Image.Image,
    bg_rgb: tuple[int, int, int],
    thresh: int,
    min_pixels: int,
    frag_max: int,
    merge_dist: float,
    merge_ratio: float,
    row_tol: float,
) -> list[tuple[int, int, int, int]]:
    """Return tight bounding boxes (x0,y0,x1,y1) in reading order."""
    rgba = image.convert("RGBA")
    width, height = rgba.size
    pixels = rgba.load()

    visited = [[False] * height for _ in range(width)]
    comps: list[dict] = []

    for y in range(height):
        for x in range(width):
            if visited[x][y]:
                continue
            if not is_foreground(pixels[x, y], bg_rgb, thresh):
                visited[x][y] = True
                continue

            queue: deque[tuple[int, int]] = deque([(x, y)])
            visited[x][y] = True
            min_x = max_x = x
            min_y = max_y = y
            count = 0

            while queue:
                cx, cy = queue.popleft()
                count += 1
                min_x = min(min_x, cx)
                max_x = max(max_x, cx)
                min_y = min(min_y, cy)
                max_y = max(max_y, cy)
                for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                    if 0 <= nx < width and 0 <= ny < height and not visited[nx][ny]:
                        visited[nx][ny] = True
                        if is_foreground(pixels[nx, ny], bg_rgb, thresh):
                            queue.append((nx, ny))

            # Keep even 1px fragments so petals/stems can merge before size filtering.
            if count >= 1:
                comps.append({"box": (min_x, min_y, max_x, max_y), "count": count})

    # Merge nearby fragments / vertically stacked parts of the same sprite.
    changed = True
    while changed:
        changed = False
        comps.sort(key=lambda c: c["count"])
        for i, comp in enumerate(comps):
            best_j = None
            best_d = None
            for j, other in enumerate(comps):
                if j == i or other["count"] < comp["count"]:
                    continue
                is_frag = comp["count"] <= frag_max
                is_smaller = comp["count"] <= other["count"] * merge_ratio
                if not (is_frag or is_smaller):
                    continue
                dist = box_gap(comp["box"], other["box"])
                if dist <= merge_dist and (best_d is None or dist < best_d):
                    best_d = dist
                    best_j = j
            if best_j is not None:
                other = comps[best_j]
                ox0, oy0, ox1, oy1 = other["box"]
                cx0, cy0, cx1, cy1 = comp["box"]
                other["box"] = (min(ox0, cx0), min(oy0, cy0), max(ox1, cx1), max(oy1, cy1))
                other["count"] += comp["count"]
                comps.pop(i)
                changed = True
                break

    comps = [c for c in comps if c["count"] >= min_pixels]
    boxes = [c["box"] for c in comps]
    return [boxes[i] for i in reading_order(boxes, row_tol)]


# ---------------------------------------------------------------------------
# Grid mode
# ---------------------------------------------------------------------------

def detect_grid(
    image: Image.Image,
    tile_w: int,
    tile_h: int,
    columns: int | None,
    margin: int,
    spacing: int,
    skip_empty: bool,
    bg_rgb: tuple[int, int, int],
    thresh: int,
) -> list[tuple[int, int, int, int]]:
    width, height = image.size
    rgba = image.convert("RGBA")
    pixels = rgba.load()

    if columns is None:
        usable_w = width - 2 * margin + spacing
        columns = max(1, (usable_w + spacing) // (tile_w + spacing))

    boxes: list[tuple[int, int, int, int]] = []
    row = 0
    while True:
        y0 = margin + row * (tile_h + spacing)
        if y0 + tile_h > height - margin + 1:
            # Allow last row to sit flush against bottom when margin is 0.
            if y0 >= height:
                break
            if y0 + tile_h > height:
                break
        for col in range(columns):
            x0 = margin + col * (tile_w + spacing)
            if x0 + tile_w > width:
                break
            x1 = x0 + tile_w - 1
            y1 = y0 + tile_h - 1
            if skip_empty:
                has_fg = False
                for y in range(y0, y1 + 1):
                    for x in range(x0, x1 + 1):
                        if is_foreground(pixels[x, y], bg_rgb, thresh):
                            has_fg = True
                            break
                    if has_fg:
                        break
                if not has_fg:
                    continue
            boxes.append((x0, y0, x1, y1))
        row += 1
        if margin + row * (tile_h + spacing) >= height:
            break
    return boxes


# ---------------------------------------------------------------------------
# Export
# ---------------------------------------------------------------------------

def crop_sprite(
    image: Image.Image,
    box: tuple[int, int, int, int],
    padding: int,
    make_transparent: bool,
    bg_rgb: tuple[int, int, int],
    thresh: int,
) -> Image.Image:
    x0, y0, x1, y1 = box
    width, height = image.size
    x0 = max(0, x0 - padding)
    y0 = max(0, y0 - padding)
    x1 = min(width - 1, x1 + padding)
    y1 = min(height - 1, y1 + padding)

    sprite = image.convert("RGBA").crop((x0, y0, x1 + 1, y1 + 1))
    if not make_transparent:
        return sprite

    pixels = sprite.load()
    sw, sh = sprite.size
    for y in range(sh):
        for x in range(sw):
            if not is_foreground(pixels[x, y], bg_rgb, thresh):
                pixels[x, y] = (0, 0, 0, 0)
    return sprite


def write_preview(image: Image.Image, boxes: list[tuple[int, int, int, int]], path: Path) -> None:
    vis = image.convert("RGBA").copy()
    draw = ImageDraw.Draw(vis)
    for i, (x0, y0, x1, y1) in enumerate(boxes):
        draw.rectangle([x0 - 1, y0 - 1, x1 + 1, y1 + 1], outline=(255, 0, 255, 255))
        draw.text((x0, max(0, y0 - 8)), str(i), fill=(255, 255, 0, 255))
    path.parent.mkdir(parents=True, exist_ok=True)
    vis.save(path)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Split a spritesheet into individual PNG files.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument("input", type=Path, help="Path to spritesheet PNG")
    p.add_argument("-o", "--output", type=Path, required=True, help="Output directory")
    p.add_argument("--mode", choices=("auto", "grid"), default="auto")
    p.add_argument("--prefix", default="sprite", help="Output filename prefix")
    p.add_argument("--padding", type=int, default=0, help="Extra pixels around each crop")
    p.add_argument(
        "--transparent",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Replace background color with alpha (default: on)",
    )
    p.add_argument(
        "--bg",
        default="0,0,0",
        help="Background RGB to treat as empty, e.g. 0,0,0 or 255,0,255",
    )
    p.add_argument(
        "--thresh",
        type=int,
        default=8,
        help="Per-channel distance from --bg to count as foreground",
    )
    p.add_argument("--preview", type=Path, help="Write a boxed preview image and exit (or also export)")
    p.add_argument(
        "--preview-only",
        action="store_true",
        help="Only write --preview, do not export sprites",
    )
    p.add_argument("--start-index", type=int, default=0, help="First output index")
    p.add_argument("--dry-run", action="store_true", help="Print boxes only")

    auto = p.add_argument_group("auto mode")
    auto.add_argument("--min-pixels", type=int, default=8, help="Ignore components smaller than this")
    auto.add_argument("--frag-max", type=int, default=25, help="Always merge components <= this size into neighbors")
    auto.add_argument("--merge-dist", type=float, default=3.0, help="Max gap (px) when merging parts")
    auto.add_argument(
        "--merge-ratio",
        type=float,
        default=0.8,
        help="Merge a component into a neighbor if it is at most this fraction of the neighbor's pixel count",
    )
    auto.add_argument("--row-tol", type=float, default=10.0, help="Y-center tolerance for reading-order rows")

    grid = p.add_argument_group("grid mode")
    grid.add_argument("--tile", default="16", help="Tile size: 16 or 16x32")
    grid.add_argument("--columns", type=int, help="Columns (default: fit image width)")
    grid.add_argument("--margin", type=int, default=0, help="Outer margin in pixels")
    grid.add_argument("--spacing", type=int, default=0, help="Gap between tiles in pixels")
    grid.add_argument(
        "--skip-empty",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Skip blank grid cells (default: on)",
    )
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)

    if not args.input.is_file():
        print(f"Input not found: {args.input}", file=sys.stderr)
        return 1

    bg_parts = [int(x.strip()) for x in args.bg.split(",")]
    if len(bg_parts) != 3:
        print("--bg must be R,G,B", file=sys.stderr)
        return 1
    bg_rgb = (bg_parts[0], bg_parts[1], bg_parts[2])

    image = Image.open(args.input)
    print(f"Loaded {args.input} ({image.size[0]}x{image.size[1]}, {image.mode})")

    if args.mode == "auto":
        boxes = detect_auto(
            image,
            bg_rgb=bg_rgb,
            thresh=args.thresh,
            min_pixels=args.min_pixels,
            frag_max=args.frag_max,
            merge_dist=args.merge_dist,
            merge_ratio=args.merge_ratio,
            row_tol=args.row_tol,
        )
    else:
        tile_w, tile_h = parse_size(args.tile)
        boxes = detect_grid(
            image,
            tile_w=tile_w,
            tile_h=tile_h,
            columns=args.columns,
            margin=args.margin,
            spacing=args.spacing,
            skip_empty=args.skip_empty,
            bg_rgb=bg_rgb,
            thresh=args.thresh,
        )

    print(f"Detected {len(boxes)} sprite(s) [{args.mode}]")
    for i, (x0, y0, x1, y1) in enumerate(boxes):
        print(f"  {i + args.start_index:03d}: {x1 - x0 + 1}x{y1 - y0 + 1} @ ({x0},{y0})")

    if args.preview:
        write_preview(image, boxes, args.preview)
        print(f"Wrote preview {args.preview}")
        if args.preview_only:
            return 0

    if args.dry_run or args.preview_only:
        return 0

    args.output.mkdir(parents=True, exist_ok=True)
    written = 0
    for i, box in enumerate(boxes):
        sprite = crop_sprite(
            image,
            box,
            padding=args.padding,
            make_transparent=args.transparent,
            bg_rgb=bg_rgb,
            thresh=args.thresh,
        )
        name = f"{args.prefix}_{i + args.start_index:03d}.png"
        out_path = args.output / name
        sprite.save(out_path)
        written += 1

    print(f"Wrote {written} file(s) to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
