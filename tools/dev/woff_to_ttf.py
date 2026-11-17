"""Convierte una fuente WOFF (1.0) a TTF/OTF, sin dependencias.

    python tools/dev/woff_to_ttf.py <entrada.woff> <salida.ttf>

Sirve para traer al studio la fuente de iconos de video-yt (material-icons de npm, que solo trae
WOFF y WOFF2): Qt carga TTF y OTF. WOFF 1.0 es un sfnt con cada tabla comprimida con zlib.
"""
import struct
import sys
import zlib
from pathlib import Path


def woff_to_sfnt(data: bytes) -> bytes:
    sig, flavor, _length, num_tables = struct.unpack(">IIIH", data[:14])
    if sig != 0x774F4646:  # 'wOFF'
        raise ValueError("no es un WOFF 1.0")
    tables = []
    for i in range(num_tables):
        tag, off, comp_len, orig_len, checksum = struct.unpack(">4sIIII", data[44 + 20 * i:64 + 20 * i])
        raw = data[off:off + comp_len]
        tables.append((tag, checksum, zlib.decompress(raw) if comp_len < orig_len else raw))
    tables.sort(key=lambda t: t[0])
    entry_selector = max(n for n in range(16) if 2 ** n <= num_tables)
    search_range = 2 ** entry_selector * 16
    out = bytearray(struct.pack(">IHHHH", flavor, num_tables, search_range, entry_selector,
                                num_tables * 16 - search_range))
    offset = 12 + 16 * num_tables
    body = bytearray()
    for tag, checksum, table in tables:
        out += struct.pack(">4sIII", tag, checksum, offset + len(body), len(table))
        body += table + b"\0" * (-len(table) % 4)
    return bytes(out + body)


if __name__ == "__main__":
    src, dst = map(Path, sys.argv[1:3])
    dst.write_bytes(woff_to_sfnt(src.read_bytes()))
    print(f"{dst}: {dst.stat().st_size} bytes")
