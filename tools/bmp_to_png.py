# SPDX-License-Identifier: MIT
"""Losslessly encode SDL's 24/32-bit BMP readback as PNG, without imaging deps."""
from pathlib import Path
import struct
import sys
import zlib


def convert(source: Path, destination: Path) -> None:
    data = source.read_bytes()
    if data[:2] != b"BM" or len(data) < 54:
        raise ValueError("not a BMP")
    offset, = struct.unpack_from("<I", data, 10)
    header, width, height, planes, bpp, compression = struct.unpack_from("<IiiHHI", data, 14)
    if header < 40 or width <= 0 or not height or planes != 1 or bpp not in (24, 32):
        raise ValueError("unsupported BMP layout")
    if compression == 3 and bpp == 32 and header >= 56:
        masks = struct.unpack_from("<IIII", data, 54)
        channels = []
        for index, mask in enumerate(masks):
            if not mask:
                if index < 3:
                    raise ValueError("missing color channel")
                continue
            shifts = [shift for shift in (0, 8, 16, 24) if mask == 255 << shift]
            if not shifts:
                raise ValueError("unsupported channel mask")
            channels.append(shifts[0] // 8)
        if len(channels) not in (3, 4):
            raise ValueError("missing color channels")
    elif compression == 0:
        channels = [2, 1, 0]
    else:
        raise ValueError("unsupported BMP compression")
    rows = abs(height)
    stride = ((width * bpp + 31) // 32) * 4
    if offset < 14 + header or offset + rows * stride > len(data):
        raise ValueError("truncated pixels")
    pixels = bytearray()
    size = bpp // 8
    for y in range(rows):
        row = offset + (rows - y - 1 if height > 0 else y) * stride
        pixels.append(0)
        for x in range(width):
            start = row + x * size
            pixels.extend(data[start + channel] for channel in channels)

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))

    encoded = b"\x89PNG\r\n\x1a\n"
    encoded += chunk(b"IHDR", struct.pack(">IIBBBBB", width, rows, 8, 6 if len(channels) == 4 else 2, 0, 0, 0))
    encoded += chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b"")
    destination.write_bytes(encoded)


if __name__ == "__main__":
    convert(Path(sys.argv[1]), Path(sys.argv[2]))
