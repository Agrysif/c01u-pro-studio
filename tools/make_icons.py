"""Draws the app and tray icons (signed distance fields, no dependencies) into assets/icons.

    python tools/make_icons.py
"""
import math
import os
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "icons")

VIOLET = (124, 92, 255)
CYAN = (34, 211, 238)
WHITE = (255, 255, 255)
GREY = (148, 156, 170)
RED = (239, 68, 68)


def clamp(v, lo=0.0, hi=1.0):
    return lo if v < lo else hi if v > hi else v


def sd_round_rect(px, py, cx, cy, hw, hh, r):
    qx = abs(px - cx) - (hw - r)
    qy = abs(py - cy) - (hh - r)
    return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - r


def sd_segment(px, py, ax, ay, bx, by, r):
    pax, pay = px - ax, py - ay
    bax, bay = bx - ax, by - ay
    h = clamp((pax * bax + pay * bay) / (bax * bax + bay * bay))
    return math.hypot(pax - bax * h, pay - bay * h) - r


def sd_lower_arc(px, py, cx, cy, radius, half_thickness):
    if py >= cy:
        return abs(math.hypot(px - cx, py - cy) - radius) - half_thickness
    # above the centre: distance to the two arc ends (round caps)
    d1 = math.hypot(px - (cx - radius), py - cy)
    d2 = math.hypot(px - (cx + radius), py - cy)
    return min(d1, d2) - half_thickness


def mic_sdf(x, y, slots):
    """Microphone glyph in a 256x256 box."""
    d = sd_segment(x, y, 128, 66, 128, 122, 40)  # head
    if slots:
        for sy in (82, 102, 122):
            d = max(d, -sd_round_rect(x, y, 128, sy, 24, 3.5, 3.5))
    d = min(d, sd_lower_arc(x, y, 128, 124, 68, 8.5))  # holder
    d = min(d, sd_round_rect(x, y, 128, 208, 8.5, 22, 2))  # stem
    d = min(d, sd_round_rect(x, y, 128, 232, 46, 9, 9))  # base
    return d


def gradient(x, y):
    t = clamp((x + y) / 512.0)
    return tuple(int(VIOLET[i] + (CYAN[i] - VIOLET[i]) * t) for i in range(3))


def over(dst, src, a):
    """Source-over blend of an RGB colour with coverage `a` onto an RGBA float pixel."""
    r, g, b, da = dst
    oa = a + da * (1 - a)
    if oa <= 0:
        return (0.0, 0.0, 0.0, 0.0)
    return (
        (src[0] * a + r * da * (1 - a)) / oa,
        (src[1] * a + g * da * (1 - a)) / oa,
        (src[2] * a + b * da * (1 - a)) / oa,
        oa,
    )


def render(size, kind):
    s = 256.0 / size  # canvas units per pixel
    rows = []
    for py in range(size):
        row = bytearray()
        for px in range(size):
            x = (px + 0.5) * s
            y = (py + 0.5) * s
            pix = (0.0, 0.0, 0.0, 0.0)
            if kind == "app":
                bg = sd_round_rect(x, y, 128, 128, 120, 120, 56)
                pix = over(pix, gradient(x, y), clamp(0.5 - bg / s))
                # glyph scaled to 66 % and centred
                gx = (x - 128) / 0.66 + 128
                gy = (y - 128) / 0.66 + 132
                d = mic_sdf(gx, gy, size >= 48) * 0.66
                pix = over(pix, WHITE, clamp(0.5 - d / s) * clamp(0.5 - bg / s))
            else:
                gx = (x - 128) / 0.92 + 128
                gy = (y - 128) / 0.92 + 128
                d = mic_sdf(gx, gy, False) * 0.92
                if kind == "tray":
                    pix = over(pix, gradient(x, y), clamp(0.5 - d / s))
                else:  # muted
                    slash = sd_segment(x, y, 52, 44, 204, 220, 13)
                    gap = sd_segment(x, y, 52, 44, 204, 220, 26)
                    a = clamp(0.5 - d / s) * clamp(0.5 + gap / s)
                    pix = over(pix, GREY, a)
                    pix = over(pix, RED, clamp(0.5 - slash / s))
            r, g, b, a = pix
            row += bytes((int(r + 0.5), int(g + 0.5), int(b + 0.5), int(a * 255 + 0.5)))
        rows.append(bytes(row))
    return rows


def png(size, rows):
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def ico(images):
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries, data = b"", b""
    for size, blob in images:
        dim = 0 if size >= 256 else size
        entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(blob), offset + len(data))
        data += blob
    return header + entries + data


def main():
    os.makedirs(OUT, exist_ok=True)
    plans = {
        "app": (16, 20, 24, 32, 40, 48, 64, 128, 256),
        "tray": (16, 20, 24, 32, 40, 48, 64),
        "tray_muted": (16, 20, 24, 32, 40, 48, 64),
    }
    for name, sizes in plans.items():
        kind = "muted" if name == "tray_muted" else name
        images = [(sz, png(sz, render(sz, kind))) for sz in sizes]
        with open(os.path.join(OUT, name + ".ico"), "wb") as f:
            f.write(ico(images))
        if name == "app":
            with open(os.path.join(OUT, "app-256.png"), "wb") as f:
                f.write(images[-1][1])
        print("wrote", name + ".ico")


if __name__ == "__main__":
    main()
