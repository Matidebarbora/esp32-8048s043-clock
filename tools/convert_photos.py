#!/usr/bin/env python3
"""Converts photos into the raw LVGL-binary format the clock's photo
slideshow (photo_slideshow_screen.cpp) reads straight off the SD card.

Why raw binaries instead of just copying JPEGs/PNGs: no PNG/JPEG decoder is
enabled in the firmware (see sdkconfig.defaults), so each image is
pre-converted here to exactly what LVGL's built-in image decoder expects for
a file-sourced LV_IMG_CF_TRUE_COLOR image — a 4-byte lv_img_header_t bitfield
(cf/always_zero/reserved/w/h, little-endian) followed by raw RGB565 pixel
data, row-major. This must match the ESP32's LV_COLOR_DEPTH (16-bit) and
lv_color16_t bit layout exactly (see managed_components/lvgl__lvgl/src/misc/
lv_color.h and src/draw/lv_img_buf.h if this format ever needs revisiting).

Usage:
    pip install pillow numpy
    python convert_photos.py                  # opens a file picker
    python convert_photos.py photo1.jpg ...    # or pass files directly
    python convert_photos.py --out ./output --interactive

Then copy every .bin file in the output folder onto the SD card's
/photos folder (create it if it doesn't exist) and insert the card.
"""
import argparse
import struct
import sys
from pathlib import Path

from PIL import Image, ImageOps
import numpy as np

SCREEN_W, SCREEN_H = 800, 480
LV_IMG_CF_TRUE_COLOR = 4


def resize_cover(img: Image.Image, target_w: int, target_h: int) -> Image.Image:
    """Scales img to fully cover target_w x target_h, cropping the overflow
    from the center — no letterboxing, matches the screen exactly."""
    src_w, src_h = img.size
    scale = max(target_w / src_w, target_h / src_h)
    new_w, new_h = round(src_w * scale), round(src_h * scale)
    img = img.resize((new_w, new_h), Image.LANCZOS)
    left = (new_w - target_w) // 2
    top = (new_h - target_h) // 2
    return img.crop((left, top, left + target_w, top + target_h))


def to_rgb565_bytes(img: Image.Image) -> bytes:
    arr = np.asarray(img, dtype=np.uint16)  # (H, W, 3), 0-255 per channel
    r = arr[:, :, 0] >> 3
    g = arr[:, :, 1] >> 2
    b = arr[:, :, 2] >> 3
    pixels = (r << 11) | (g << 5) | b
    return pixels.astype("<u2").tobytes()  # little-endian, row-major


def make_lv_img_header(w: int, h: int) -> bytes:
    # Matches lv_img_header_t's bitfield layout (lv_img_buf.h): cf(5) |
    # always_zero(3) | reserved(2) | w(11) | h(11), packed LSB-first as a
    # single little-endian uint32 (GCC's default bitfield order on this
    # little-endian target).
    value = (LV_IMG_CF_TRUE_COLOR & 0x1F) | ((w & 0x7FF) << 10) | ((h & 0x7FF) << 21)
    return struct.pack("<I", value)


def convert_one(src_path: Path, dst_path: Path) -> None:
    img = Image.open(src_path)
    img = ImageOps.exif_transpose(img)  # respect phone-camera rotation tags
    img = img.convert("RGB")
    img = resize_cover(img, SCREEN_W, SCREEN_H)

    header = make_lv_img_header(SCREEN_W, SCREEN_H)
    pixels = to_rgb565_bytes(img)
    dst_path.write_bytes(header + pixels)


def pick_files_interactively() -> list[Path]:
    import tkinter as tk
    from tkinter import filedialog

    root = tk.Tk()
    root.withdraw()
    paths = filedialog.askopenfilenames(
        title="Select photos for the clock's SD card slideshow",
        filetypes=[("Images", "*.jpg *.jpeg *.png *.bmp *.webp"), ("All files", "*.*")],
    )
    return [Path(p) for p in paths]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("photos", nargs="*", help="Photo files to convert (omit to open a file picker)")
    parser.add_argument("--out", default="sd_photos", help="Output folder (default: ./sd_photos)")
    args = parser.parse_args()

    sources = [Path(p) for p in args.photos] if args.photos else pick_files_interactively()
    if not sources:
        print("No photos selected — nothing to do.")
        sys.exit(0)

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    for i, src in enumerate(sources, start=1):
        dst = out_dir / f"{i:04d}.bin"
        try:
            convert_one(src, dst)
            print(f"  {src.name} -> {dst.name}")
        except Exception as e:
            print(f"  SKIPPED {src.name}: {e}", file=sys.stderr)

    print(f"\nDone. Copy every .bin file in {out_dir}/ onto the SD card's /photos folder, then insert it.")


if __name__ == "__main__":
    main()
