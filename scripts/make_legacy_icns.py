#!/usr/bin/env python3
"""Build a .icns containing the classic pre-Leopard raw-bitmap icon
formats (is32/il32/it32 + s8mk/l8mk/t8mk masks) that Tiger's Finder
actually understands, plus a modern ic08 (256x256 PNG) for contemporary
macOS. `iconutil` (Apple's own modern tool) only emits the newer
PNG-based family codes (ic04/ic07/ic08/...), which pre-Leopard Finder
can't parse at all -- confirmed by decomposing a known-working PPC .app
icon (AmuletsArmor's amulets.icns) and finding only classic formats
plus one ic08, no ic04/ic05 etc.

Classic 32-bit icon chunks store each of the R/G/B channels as a
separate PackBits-compressed plane (concatenated); the *8mk mask
chunks store a raw (uncompressed) 8-bit alpha plane.
"""
import struct
import sys
from PIL import Image


def packbits_encode(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        # try a run of identical bytes
        run = 1
        while i + run < n and data[i + run] == data[i] and run < 128:
            run += 1
        if run >= 3:
            out.append(257 - run if run <= 128 else 129)  # (1-run) as unsigned byte, run in 3..128
            out.append(data[i])
            i += run
            continue
        # literal run: gather bytes until next run of >=3 identical or end/128 cap
        start = i
        i += 1
        length = 1
        while i < n and length < 128:
            # stop the literal run if the next 3 bytes form a repeat run
            if i + 2 < n and data[i] == data[i + 1] == data[i + 2]:
                break
            i += 1
            length += 1
        out.append(length - 1)
        out.extend(data[start:start + length])
    return bytes(out)


def plane_bytes(img: Image.Image, channel: int) -> bytes:
    return bytes(px[channel] for px in img.getdata())


def build_classic_chunk(png_path: str, rgb_type: str, mask_type: str, size: int) -> bytes:
    img = Image.open(png_path).convert("RGBA")
    if img.size != (size, size):
        img = img.resize((size, size), Image.LANCZOS)

    r = packbits_encode(plane_bytes(img, 0))
    g = packbits_encode(plane_bytes(img, 1))
    b = packbits_encode(plane_bytes(img, 2))
    rgb_data = r + g + b

    alpha = bytes(px[3] for px in img.getdata())

    rgb_chunk = rgb_type.encode() + struct.pack(">I", 8 + len(rgb_data)) + rgb_data
    mask_chunk = mask_type.encode() + struct.pack(">I", 8 + len(alpha)) + alpha
    return rgb_chunk + mask_chunk


def build_modern_chunk(png_path: str, code: str, size: int) -> bytes:
    img = Image.open(png_path).convert("RGBA")
    if img.size != (size, size):
        img = img.resize((size, size), Image.LANCZOS)
    import io
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    png_data = buf.getvalue()
    return code.encode() + struct.pack(">I", 8 + len(png_data)) + png_data


def main():
    iconset_dir, out_path = sys.argv[1], sys.argv[2]

    chunks = b""
    chunks += build_classic_chunk(f"{iconset_dir}/icon_16.png", "is32", "s8mk", 16)
    chunks += build_classic_chunk(f"{iconset_dir}/icon_32.png", "il32", "l8mk", 32)
    chunks += build_classic_chunk(f"{iconset_dir}/icon_128.png", "it32", "t8mk", 128)
    chunks += build_modern_chunk(f"{iconset_dir}/icon_256.png", "ic08", 256)

    body = b"icns" + struct.pack(">I", 8 + len(chunks)) + chunks
    with open(out_path, "wb") as f:
        f.write(body)


if __name__ == "__main__":
    main()
