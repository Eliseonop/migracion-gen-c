"""Casos sintéticos de la lectura de cabeceras de imagen (images.py: probe_gif y _ext_from_magic).

Los usa export_fixtures.py → tests/fixtures/media/synthetic.json. Los GIF se generan con
Pillow (el del venv de video-yt) y a mano; de algunos se prueban TODOS sus truncados.
"""
from __future__ import annotations

import io


def _pillow_gif(frames: int, size=(4, 3), **save) -> bytes:
    from PIL import Image
    imgs = [Image.new("P", size, color=i % 4) for i in range(frames)]
    for im in imgs:
        im.putpalette([0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255] + [0] * (256 * 3 - 12))
    buf = io.BytesIO()
    if frames == 1:
        imgs[0].save(buf, format="GIF", **save)
    else:
        imgs[0].save(buf, format="GIF", save_all=True, append_images=imgs[1:], **save)
    return buf.getvalue()


def _block(data: bytes) -> bytes:
    """Sub-bloques de 255 como mucho, terminados en 0."""
    out = b""
    for i in range(0, len(data), 255):
        chunk = data[i:i + 255]
        out += bytes([len(chunk)]) + chunk
    return out + b"\x00"


def _gce(delay_cs: int, transparent: bool = False) -> bytes:
    return b"\x21\xf9\x04" + bytes([0x01 if transparent else 0x00]) + delay_cs.to_bytes(2, "little") + b"\x00\x00"


def _netscape(loops: int, sub: bytes = b"\x03\x01") -> bytes:
    """Extensión de aplicación NETSCAPE2.0: sub-bloque de 3 bytes (id, contador) y fin."""
    return b"\x21\xff\x0bNETSCAPE2.0" + sub + loops.to_bytes(2, "little") + b"\x00"


def _image(local_table_bits: int | None = None) -> bytes:
    packed = 0 if local_table_bits is None else 0x80 | local_table_bits
    table = b"" if local_table_bits is None else b"\x00" * (3 * 2 ** (local_table_bits + 1))
    return b"\x2c" + b"\x00" * 8 + bytes([packed]) + table + b"\x02" + _block(b"\x44\x01")


def _gif(width: int, height: int, body: bytes, global_bits: int | None = 0, magic: bytes = b"GIF89a") -> bytes:
    packed = 0 if global_bits is None else 0x80 | global_bits
    table = b"" if global_bits is None else b"\x00" * (3 * 2 ** (global_bits + 1))
    return magic + width.to_bytes(2, "little") + height.to_bytes(2, "little") + bytes([packed, 0, 0]) + table + body


def gif_cases() -> list[tuple[str, bytes]]:
    two = _gce(10) + _image() + _gce(25, True) + _image(1)
    cases = [
        ("vacío", b""),
        ("solo la firma", b"GIF89a"),
        ("12 bytes", b"GIF89a\x01\x00\x01\x00\x00\x00"),
        ("cabecera sin bloques", _gif(5, 7, b"")),
        ("GIF87a de un fotograma", _gif(2, 2, _image() + b"\x3b", magic=b"GIF87a")),
        ("firma desconocida", b"GIF90a" + b"\x00" * 20),
        ("PNG", b"\x89PNG\r\n\x1a\n" + b"\x00" * 20),
        ("dos fotogramas sin NETSCAPE", _gif(320, 240, two + b"\x3b")),
        ("dos fotogramas, NETSCAPE infinito", _gif(16, 9, _netscape(0) + two + b"\x3b")),
        ("dos fotogramas, NETSCAPE 3 veces", _gif(16, 9, _netscape(3) + two + b"\x3b")),
        ("NETSCAPE con sub-bloque raro", _gif(16, 9, _netscape(0, b"\x03\x02") + two + b"\x3b")),
        ("NETSCAPE sin contador", _gif(16, 9, b"\x21\xff\x0bNETSCAPE2.0\x03\x01")),
        ("un fotograma con NETSCAPE", _gif(16, 9, _netscape(0) + _gce(0) + _image() + b"\x3b")),
        ("retardo 0 vale 0,1", _gif(8, 8, _gce(0) + _image() + _gce(0) + _image() + _gce(0) + _image() + b"\x3b")),
        ("retardos que no suman exacto", _gif(8, 8, b"".join(_gce(d) + _image() for d in (3, 7, 1, 33, 10, 1)) + b"\x3b")),
        ("fotograma sin GCE tras uno con retardo", _gif(8, 8, _gce(50) + _image() + _image() + b"\x3b")),
        ("GCE sin fotograma detrás",_gif(8, 8, _image() + _gce(50, True) + b"\x3b")),
        ("GCE corta al final", _gif(8, 8, _image() + b"\x21\xf9\x04\x01\x05")),
        ("sin trailer", _gif(8, 8, two)),
        ("bytes basura entre bloques", _gif(8, 8, b"\x00\x07" + two + b"\x99\x3b")),
        ("extensión de comentario", _gif(8, 8, b"\x21\xfe" + _block(b"hola mundo" * 40) + two + b"\x3b")),
        ("tabla global de 256", _gif(8, 8, two + b"\x3b", global_bits=7)),
        ("sin tabla global", _gif(8, 8, two + b"\x3b", global_bits=None)),
        ("tabla global más larga que el archivo", _gif(8, 8, b"", global_bits=7)[:40]),
        ("dimensiones grandes", _gif(65535, 1, _image() + b"\x3b")),
        ("descriptor cortado", _gif(8, 8, b"\x2c\x00\x00")),
        ("sub-bloque más largo que el archivo", _gif(8, 8, _image()[:-4] + b"\xff\x01")),
        ("muchos fotogramas", _gif(8, 8, b"".join(_gce(4) + _image() for _ in range(300)) + b"\x3b")),
        ("Pillow: estático", _pillow_gif(1)),
        ("Pillow: estático con transparencia", _pillow_gif(1, transparency=0)),
        ("Pillow: 3 fotogramas, loop 0", _pillow_gif(3, duration=120, loop=0)),
        ("Pillow: 4 fotogramas, loop 2", _pillow_gif(4, duration=[40, 50, 60, 70], loop=2)),
        ("Pillow: 5 fotogramas sin loop", _pillow_gif(5, duration=30)),
        ("Pillow: transparencia y disposal", _pillow_gif(3, duration=80, loop=0, transparency=0, disposal=2)),
        ("Pillow: 2 fotogramas grandes", _pillow_gif(2, size=(300, 200), duration=1000)),
    ]
    # Todos los truncados de dos GIF pequeños: cada prefijo, del 0 al archivo entero.
    for name, data in (("trunc NETSCAPE", _gif(16, 9, _netscape(0) + two + b"\x3b")),
                       ("trunc Pillow", _pillow_gif(3, duration=70, loop=0, transparency=0))):
        cases += [(f"{name}[:{i}]", data[:i]) for i in range(len(data) + 1)]
    return cases


# _ext_from_magic: firmas completas, cortadas y casi iguales.
MAGIC = [
    b"", b"\x89PNG\r\n\x1a\n", b"\x89PNG\r\n\x1a", b"\x89PNG\r\n\x1a\nresto", b"\xff\xd8\xff", b"\xff\xd8\xffe0",
    b"\xff\xd8", b"\xff\xd9\xff", b"GIF87a", b"GIF89a....", b"GIF8", b"GIF88a", b"RIFF\x00\x00\x00\x00WEBP",
    b"RIFF\x00\x00\x00\x00WEBPVP8 ", b"RIFF\x00\x00\x00\x00WEB", b"RIFF\x00\x00\x00\x00WAVE", b"RIFX\x00\x00\x00\x00WEBP",
    b"\x00\x00\x00\x18ftypavif", b"BM", b"<svg", b"GIF89a\x89PNG",
]
