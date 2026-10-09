#!/usr/bin/env python3
"""Generate AlphaSnipe PNG and ICO icons."""
import math
import os
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")


def write_png(path, w, h, rgba):
    def chunk(tag, data):
        crc = zlib.crc32(tag + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)

    raw = b"".join(b"\x00" + bytes(rgba[y * w * 4 : (y + 1) * w * 4]) for y in range(h))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def write_ico(path, images):
    # images: list of (w, h, rgba_bytes)
    count = len(images)
    pngs = []
    for w, h, rgba in images:
        import io

        buf = io.BytesIO()
        # reuse write_png into memory
        def chunk(tag, data):
            crc = zlib.crc32(tag + data) & 0xFFFFFFFF
            return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)

        raw = b"".join(b"\x00" + bytes(rgba[y * w * 4 : (y + 1) * w * 4]) for y in range(h))
        buf.write(b"\x89PNG\r\n\x1a\n")
        buf.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        buf.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        buf.write(chunk(b"IEND", b""))
        pngs.append(buf.getvalue())

    offset = 6 + 16 * count
    data = bytearray(struct.pack("<HHH", 0, 1, count))
    for (w, h, _), png in zip(images, pngs):
        data += struct.pack(
            "<BBBBHHII",
            w if w < 256 else 0,
            h if h < 256 else 0,
            0,
            0,
            1,
            32,
            len(png),
            offset,
        )
        offset += len(png)
    for png in pngs:
        data += png
    with open(path, "wb") as f:
        f.write(data)


def lerp(a, b, t):
    return int(a + (b - a) * t)


def setp(px, w, h, x, y, r, g, b, a):
    if 0 <= x < w and 0 <= y < h and a > 0:
        i = (y * w + x) * 4
        if a >= 255:
            px[i : i + 4] = bytes((r, g, b, 255))
        else:
            da = px[i + 3]
            if da == 0:
                px[i : i + 4] = bytes((r, g, b, a))
            else:
                out_a = a + da * (255 - a) // 255
                inv = 255 - a
                px[i] = (r * a + px[i] * inv) // 255
                px[i + 1] = (g * a + px[i + 1] * inv) // 255
                px[i + 2] = (b * a + px[i + 2] * inv) // 255
                px[i + 3] = out_a


def dist_round_rect(px, py, cx, cy, hw, hh, rad):
    dx = abs(px - cx) - (hw - rad)
    dy = abs(py - cy) - (hh - rad)
    ox = max(dx, 0)
    oy = max(dy, 0)
    inside = min(max(dx, dy), 0)
    return math.hypot(ox, oy) + inside - rad


def cover(d):
    if d <= -0.5:
        return 255
    if d >= 0.5:
        return 0
    return int(255 * (0.5 - d))


def draw_icon(size):
    px = bytearray(size * size * 4)
    s = size
    cx = cy = (s - 1) / 2.0
    # outer rounded square
    hw = hh = s * 0.46
    rad = s * 0.18
    for y in range(s):
        for x in range(s):
            d = dist_round_rect(x, y, cx, cy, hw, hh, rad)
            a = cover(d)
            if a:
                # slight vertical gradient
                t = y / max(s - 1, 1)
                r = lerp(0x00, 0x00, t)
                g = lerp(0x78, 0x5A, t)
                b = lerp(0xD4, 0xB8, t)
                # inner highlight
                inner = dist_round_rect(x, y, cx, cy - s * 0.04, hw * 0.92, hh * 0.72, rad * 0.9)
                if inner < 0:
                    k = min(1.0, -inner / (s * 0.08))
                    r = lerp(r, 0x3A, k * 0.25)
                    g = lerp(g, 0xA0, k * 0.25)
                    b = lerp(b, 0xE8, k * 0.25)
                setp(px, s, s, x, y, r, g, b, a)

    # crop-mark rectangle
    def stroke_round_rect(x0, y0, x1, y1, radius, thickness, col, gap_frac=0.22):
        mx, my = (x0 + x1) / 2, (y0 + y1) / 2
        w, h = (x1 - x0), (y1 - y0)
        for y in range(s):
            for x in range(s):
                d = dist_round_rect(x, y, mx, my, w / 2, h / 2, radius)
                ring = abs(d) - thickness / 2
                a = cover(ring)
                if not a:
                    continue
                # crop marks: keep corners, hide mid-sides
                nx = abs(x - mx) / max(w / 2, 1)
                ny = abs(y - my) / max(h / 2, 1)
                # near a corner if both nx and ny are large, or along short segments
                corner = (nx > 1 - gap_frac * 2 and ny > 0.15) or (ny > 1 - gap_frac * 2 and nx > 0.15)
                # also keep the L-shaped corners more generously
                on_left = x < mx - w * 0.18
                on_right = x > mx + w * 0.18
                on_top = y < my - h * 0.18
                on_bot = y > my + h * 0.18
                keep = (on_left and on_top) or (on_right and on_top) or (on_left and on_bot) or (on_right and on_bot)
                keep = keep or corner
                if not keep:
                    continue
                setp(px, s, s, x, y, col[0], col[1], col[2], min(a, col[3]))

    m = int(s * 0.22)
    stroke_round_rect(m, m, s - m, s - m, s * 0.04, max(2.2, s * 0.055), (255, 255, 255, 240))

    # inner filled selection (translucent)
    ix0, iy0 = s * 0.32, s * 0.34
    ix1, iy1 = s * 0.70, s * 0.68
    mx, my = (ix0 + ix1) / 2, (iy0 + iy1) / 2
    hw, hh = (ix1 - ix0) / 2, (iy1 - iy0) / 2
    for y in range(s):
        for x in range(s):
            d = dist_round_rect(x, y, mx, my, hw, hh, s * 0.02)
            a = cover(d)
            if a:
                setp(px, s, s, x, y, 255, 255, 255, int(a * 0.18))
            ring = abs(d) - max(1.0, s * 0.018)
            a2 = cover(ring)
            if a2:
                setp(px, s, s, x, y, 255, 255, 255, min(230, a2))

    return px


def main():
    os.makedirs(ASSETS, exist_ok=True)
    sizes = [256, 64, 48, 32, 16]
    images = []
    for sz in sizes:
        rgba = draw_icon(sz)
        images.append((sz, sz, rgba))
        if sz == 256:
            write_png(os.path.join(ASSETS, "alphasnipe.png"), sz, sz, rgba)
        if sz == 48:
            write_png(os.path.join(ASSETS, "alphasnipe-48.png"), sz, sz, rgba)
    write_ico(os.path.join(ASSETS, "alphasnipe.ico"), images)
    print("wrote icons")


if __name__ == "__main__":
    main()
