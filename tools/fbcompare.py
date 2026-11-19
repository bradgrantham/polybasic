#!/usr/bin/env python3
"""Compare a Griffin Jr. framebuffer page with a reference PNG.

usage: fbcompare.py REFERENCE.png EMULATOR-LOG

The log is what the Griffin Jr. emulator wrote to stderr when run with
--dump-mem for both framebuffer pages (76800 bytes each, RGB332).  The PNG is
one the host build wrote with --png: 320x240, 8 bits, RGB or palette.  Exits 0 if either
page is exactly the PNG's picture, and says which; otherwise reports how many
pixels of the nearer page differ.
"""
import re
import struct
import sys
import zlib

W, H = 320, 240


def png_rgb332(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, header, palette = 8, b"", None, b""
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            palette = body
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    width, height, depth, colour = header[:4]
    assert (width, height, depth) == (W, H, 8) and colour in (2, 3), \
        "expected a 320x240 8-bit RGB or palette PNG, got %r" % (header,)
    size = 3 if colour == 2 else 1
    raw = zlib.decompress(idat)
    stride = 1 + W * size
    out = bytearray()
    for y in range(H):
        row = raw[y * stride:(y + 1) * stride]
        assert row[0] == 0, "PNG row filter %d is not handled" % row[0]
        for x in range(W):
            if colour == 2:
                r, g, b = row[1 + x * 3:4 + x * 3]
            else:
                r, g, b = palette[row[1 + x] * 3:row[1 + x] * 3 + 3]
            # The host expands 3, 3 and 2 bits to 8; undo that.
            out.append((r * 7 + 127) // 255 << 5 | (g * 7 + 127) // 255 << 2 | (b * 3 + 127) // 255)
    return bytes(out)


def dumped_pages(path):
    memory = {}
    for line in open(path, errors="replace"):
        m = re.match(r"([0-9A-F]{6}): ((?:[0-9A-F]{2} +)+)", line)
        if m:
            address = int(m.group(1), 16)
            for k, byte in enumerate(m.group(2).split()):
                memory[address + k] = int(byte, 16)
    pages = []
    start = min(memory) if memory else 0
    while all(start + k in memory for k in (0, W * H - 1)):
        pages.append(bytes(memory[start + k] for k in range(W * H)))
        start += W * H
    return pages


def main():
    want = png_rgb332(sys.argv[1])
    pages = dumped_pages(sys.argv[2])
    if not pages:
        print("no framebuffer dump in", sys.argv[2])
        return 1
    best = None
    for n, page in enumerate(pages):
        differ = sum(1 for a, b in zip(want, page) if a != b)
        if differ == 0:
            return 0
        if best is None or differ < best[0]:
            best = (differ, n)
    print("%d of %d pixels differ from the nearer page (dump %d)" % (best[0], W * H, best[1]))
    return 1


sys.exit(main())
