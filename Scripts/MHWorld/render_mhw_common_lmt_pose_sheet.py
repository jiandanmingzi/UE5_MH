"""Render read-only skeletal contact sheets from MHW LMT pose samples.

This consumes the JSON emitted by audit_mhw_common_fall_pose_cycles.py and
writes PNG review sheets under Saved. It does not read or modify game assets,
export FBX, or touch Unreal Content.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--rows-per-page", type=int, default=8)
    return parser.parse_args()


def load_font(size):
    for path in (
        Path("C:/Windows/Fonts/arial.ttf"),
        Path("C:/Windows/Fonts/segoeui.ttf"),
    ):
        if path.is_file():
            return ImageFont.truetype(str(path), size)
    return ImageFont.load_default()


def all_points(actions):
    points = []
    for action in actions:
        for frame in action["preview_frames"]:
            points.extend(frame["points_cm"].values())
    return points


def view_bounds(points, axis_x, axis_y):
    xs = [point[axis_x] for point in points]
    ys = [point[axis_y] for point in points]
    return min(xs), max(xs), min(ys), max(ys)


def draw_pose(draw, frame, edges, view, box, scale_bounds, fonts):
    x0, y0, x1, y1 = box
    draw.rectangle(box, fill=(247, 248, 250), outline=(205, 209, 216), width=1)
    axis_x, axis_y, label = view
    min_x, max_x, min_y, max_y = scale_bounds
    usable_w = max(1, x1 - x0 - 16)
    usable_h = max(1, y1 - y0 - 25)
    width = max(1.0, max_x - min_x)
    height = max(1.0, max_y - min_y)
    scale = min(usable_w / width, usable_h / height)
    center_x = x0 + (x1 - x0) * 0.5
    center_y = y0 + 17 + usable_h * 0.5
    points = frame["points_cm"]

    def project(point):
        return (center_x + (point[axis_x] - (min_x + max_x) * 0.5) * scale,
                center_y - (point[axis_y] - (min_y + max_y) * 0.5) * scale)

    for parent, child in edges:
        if parent in points and child in points:
            draw.line((project(points[parent]), project(points[child])),
                      fill=(52, 71, 93), width=2)
    radius = 2
    for point in points.values():
        px, py = project(point)
        draw.ellipse((px - radius, py - radius, px + radius, py + radius),
                     fill=(198, 74, 64), outline=(120, 35, 30))
    draw.text((x0 + 4, y0 + 3), label, fill=(40, 45, 52), font=fonts["small"])


def main():
    args = parse_args()
    if not args.input.is_file():
        raise FileNotFoundError(args.input)
    if args.rows_per_page <= 0:
        raise ValueError("rows-per-page must be positive")
    if args.output_dir.exists() and any(args.output_dir.glob("page_*.png")):
        raise FileExistsError(f"Refusing to overwrite existing contact sheets: {args.output_dir}")
    args.output_dir.mkdir(parents=True, exist_ok=True)

    report = json.loads(args.input.read_text(encoding="utf-8"))
    actions = sorted(report["actions"], key=lambda item: (
        item["best_cycle_candidates"][0]["advisory_rank_cm_plus_0p20_deg"]
        if item["best_cycle_candidates"] else float("inf"),
        item["slot"],
    ))
    edges = report["bone_edges"]
    bounds_points = all_points(actions)
    if not actions or not bounds_points:
        raise RuntimeError("No pose points found in the preview input")
    views = ((0, 2, "XZ front"), (1, 2, "YZ side"))
    bounds = [view_bounds(bounds_points, view[0], view[1]) for view in views]

    label_w = 190
    cell_w = 124
    row_h = 188
    top_h = 82
    sheet_w = label_w + 10 * cell_w
    fonts = {"title": load_font(20), "label": load_font(16), "small": load_font(12)}
    source_hash = report["sources"]["lmt"]["sha256"]
    source_label = report.get("source_label", "unknown LMT")

    for page_index, start in enumerate(range(0, len(actions), args.rows_per_page), 1):
        page_actions = actions[start:start + args.rows_per_page]
        sheet_h = top_h + len(page_actions) * row_h
        image = Image.new("RGB", (sheet_w, sheet_h), (232, 235, 239))
        draw = ImageDraw.Draw(image)
        draw.text((12, 10), f"MHW {source_label} pose screen — page {page_index} / "
                  f"{(len(actions) + args.rows_per_page - 1) // args.rows_per_page}",
                  fill=(25, 30, 36), font=fonts["title"])
        draw.text((12, 40), f"LMT SHA256 {source_hash} | skeletal projections only | slots are not semantic labels",
                  fill=(60, 66, 74), font=fonts["small"])

        for frame_column, frame_label in enumerate(("0%", "25%", "50%", "75%", "100%")):
            col_x = label_w + frame_column * 2 * cell_w
            draw.text((col_x + 6, 61), f"{frame_label} — XZ / YZ", fill=(45, 51, 58), font=fonts["label"])

        for row, action in enumerate(page_actions):
            y0 = top_h + row * row_h
            draw.rectangle((0, y0, sheet_w, y0 + row_h - 1), fill=(255, 255, 255),
                           outline=(196, 201, 208), width=1)
            cycle = action["best_cycle_candidates"][0] if action["best_cycle_candidates"] else None
            draw.text((10, y0 + 8), f"{action['action_name']}", fill=(25, 30, 36), font=fonts["label"])
            draw.text((10, y0 + 31), f"{action['frame_count']} frames", fill=(70, 76, 84), font=fonts["small"])
            if cycle:
                draw.text((10, y0 + 50),
                          f"cycle {cycle['start_frame_inclusive']}:{cycle['end_frame_exclusive']}",
                          fill=(70, 76, 84), font=fonts["small"])
                draw.text((10, y0 + 68),
                          f"repeat {cycle['mean_repeated_position_error_cm']:.2f} cm",
                          fill=(70, 76, 84), font=fonts["small"])

            frames = action["preview_frames"]
            for frame_index, frame in enumerate(frames):
                col = frame_index * 2
                x = label_w + col * cell_w
                draw.text((x + 4, y0 + 5), f"f{frame['frame']}", fill=(75, 80, 88), font=fonts["small"])
                draw_pose(draw, frame, edges, views[0],
                          (x + 2, y0 + 24, x + cell_w - 2, y0 + row_h - 5), bounds[0], fonts)
                draw_pose(draw, frame, edges, views[1],
                          (x + cell_w + 2, y0 + 24, x + 2 * cell_w - 2, y0 + row_h - 5), bounds[1], fonts)

        path = args.output_dir / f"page_{page_index:02d}.png"
        image.save(path, optimize=True)
        print(f"CONTACT_SHEET={path} SIZE={image.width}x{image.height}")

    print(f"ACTION_COUNT={len(actions)}")
    print("LIMIT=visual shortlist only; slot semantics remain unverified")


if __name__ == "__main__":
    main()
