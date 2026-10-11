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


# Medios pequeños para las sondas de ffprobe (sound_design.probe_duration, compose._color_untagged
# y compose._has_audio). Se generan UNA vez con el ffmpeg del PATH en tests/fixtures/media/files
# (git-lfs) y luego se reutilizan: regenerarlos cambiaría los bytes (versión del encoder).
_V = ["-f", "lavfi", "-i", "testsrc2=size=64x48:rate=25"]
_A = ["-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100"]
_TAG709 = ["-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "bt709"]
PROBE_MEDIA = [
    # (archivo, argumentos de ffmpeg entre las entradas y el archivo de salida)
    ("v_untagged.mp4", _V + ["-t", "1", "-c:v", "libx264", "-pix_fmt", "yuv420p"]),
    ("v_bt709.mp4", _V + ["-t", "1", "-c:v", "libx264", "-pix_fmt", "yuv420p"] + _TAG709),
    ("v_smpte170m.mov", _V + ["-t", "0.52", "-c:v", "mpeg4", "-colorspace", "smpte170m"]),
    ("va_bt709.mp4", _V + _A + ["-t", "1.2345", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac"] + _TAG709),
    ("va_untagged.mkv", _V + _A + ["-t", "0.7", "-c:v", "libvpx", "-c:a", "libopus"]),
    ("a.wav", _A + ["-t", "0.3333", "-c:a", "pcm_s16le"]),
    ("a.mp3", _A + ["-t", "0.5", "-c:a", "libmp3lame"]),
    ("a.m4a", _A + ["-t", "2.0005", "-c:a", "aac"]),
    ("still.png", _V + ["-frames:v", "1"]),
    ("anim.gif", _V + ["-t", "0.4"]),
    ("mjpeg_vbr.avi", _V + _A + ["-t", "0.36", "-c:v", "mjpeg", "-c:a", "pcm_s16le"]),
    # Varios streams: `-select_streams v:0` toma el PRIMER vídeo (también una carátula) y
    # `has_audio` cualquier audio, esté donde esté.
    ("a_cover.mp3", _A + ["-f", "lavfi", "-i", "testsrc2=size=32x32:rate=1", "-map", "0:a", "-map", "1:v",
                          "-t", "0.5", "-frames:v", "1", "-c:a", "libmp3lame", "-c:v", "mjpeg",
                          "-disposition:v", "attached_pic"]),
    ("av_audio_first.mkv", _A + _V + ["-map", "0:a", "-map", "1:v", "-t", "0.6", "-c:a", "libopus",
                                      "-c:v", "libvpx"]),
    ("vv_two.mkv", _V + ["-f", "lavfi", "-i", "testsrc=size=64x48:rate=25", "-map", "0:v", "-map", "1:v",
                         "-t", "0.4", "-c:v", "mpeg4", "-colorspace:v:0", "bt709", "-colorspace:v:1", "smpte170m"]),
    ("vv_two_untagged_first.mkv", _V + ["-f", "lavfi", "-i", "testsrc=size=64x48:rate=25", "-map", "0:v",
                                        "-map", "1:v", "-t", "0.4", "-c:v", "mpeg4", "-colorspace:v:1", "bt709"]),
]
# VFR con un hueco de 1 s (como una grabación de pantalla): fotogramas 0–9 a 25 fps, luego
# nada hasta 1,4 s y otros 10. Se añade aparte para no cambiar los de arriba.
PROBE_MEDIA_VFR = [
    ("v_vfr_gap.mp4", _V + ["-frames:v", "20", "-vf", "setpts='if(lt(N,10),N,N+25)/25/TB'", "-fps_mode", "vfr",
                            "-c:v", "libx264", "-pix_fmt", "yuv420p"]),
]

# Derivados de otros medios de PROBE_DIR (revisión del decode): contenedores que no empiezan en 0
# (el CLI resta su start_time) y un H.264 de 2 s con 300 bytes invertidos a mitad (el CLI salta
# los paquetes rotos y sigue). (archivo, origen, argumentos entre `-i origen` y la salida.)
PROBE_DERIVED = [
    ("v_offset.mkv", "v_untagged.mp4", ["-itsoffset", "0.5"], ["-c", "copy"]),
    ("va_offset.mp4", "va_bt709.mp4", ["-itsoffset", "0.3"], ["-c", "copy"]),
]
PROBE_MEDIA_2S = [("v_2s.mp4", _V + ["-t", "2", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-g", "10"])]
CORRUPT = ("v_corrupt.mp4", "v_2s.mp4", 300)  # (archivo, origen, bytes invertidos a mitad)

# Decode como el export (compose.py): (nombre, archivo, clip, fps). El clip se completa con
# source_duration = la duración que da probe_duration; el exportador arma la cadena con las
# funciones REALES (ffmpeg_input_args, ffmpeg_trim_filter, _color_untagged).
DECODE = [
    ("mp4 entero a 30", "v_untagged.mp4", {"kind": "video", "in_point": 0.0, "out_point": 1.0}, 30),
    ("mp4 tramo a 24", "v_untagged.mp4", {"kind": "video", "in_point": 0.21, "out_point": 0.79}, 24),
    ("mp4 bt709 a 25", "v_bt709.mp4", {"kind": "video", "in_point": 0.1, "out_point": 0.6}, 25),
    ("mp4 hasta el final a 60", "va_bt709.mp4", {"kind": "video", "in_point": 0.9, "out_point": 1.24}, 60),
    ("mov smpte170m", "v_smpte170m.mov", {"kind": "video", "in_point": 0.0, "out_point": 0.52}, 30),
    ("mkv vp8", "va_untagged.mkv", {"kind": "video", "in_point": 0.2, "out_point": 0.5}, 30),
    ("avi mjpeg", "mjpeg_vbr.avi", {"kind": "video", "in_point": 0.0, "out_point": 0.36}, 30),
    ("vfr antes del hueco", "v_vfr_gap.mp4", {"kind": "video", "in_point": 0.0, "out_point": 0.3}, 30),
    ("vfr dentro del hueco", "v_vfr_gap.mp4", {"kind": "video", "in_point": 0.5, "out_point": 1.2}, 30),
    ("vfr cruzando el hueco", "v_vfr_gap.mp4", {"kind": "video", "in_point": 0.3, "out_point": 1.6}, 24),
    ("png en bucle", "still.png", {"kind": "image", "asset_kind": "images", "in_point": 0.0, "out_point": 0.4}, 30),
    ("gif en bucle", "anim.gif", {"kind": "image", "asset_kind": "images", "in_point": 0.0, "out_point": 1.0}, 25),
    ("gif una vez", "anim.gif", {"kind": "image", "asset_kind": "images", "in_point": 0.0, "out_point": 1.0,
                                 "loop": False}, 25),
    ("mkv que empieza en 0,5", "v_offset.mkv", {"kind": "video", "in_point": 0.0, "out_point": 0.8}, 30),
    ("mp4 que empieza en 0,3", "va_offset.mp4", {"kind": "video", "in_point": 0.1, "out_point": 0.9}, 25),
    ("h264 con paquetes rotos", "v_corrupt.mp4", {"kind": "video", "in_point": 0.0, "out_point": 2.0}, 25),
]

# Fotograma de un instante como frame_grab.extract_frame (`-ss t` antes de `-i`, sin el escalado
# ni el JPEG): (archivo, instantes). Justo en un fotograma, entre dos, negativos, NaN, más allá
# del final y dentro del hueco del VFR.
SEEK = [
    ("v_untagged.mp4", [0.0, 0.02, 0.04, 0.0400001, 0.0404, 0.0405, 0.079, 0.08, 0.081, 0.5, 0.96, 0.961, 0.999, 1.0, 5.0,
                        -1.0, float("nan")]),
    ("va_bt709.mp4", [0.0, 0.333, 1.2, 1.23, 1.24]),
    ("v_smpte170m.mov", [0.0, 0.1, 0.26, 0.5]),
    ("va_untagged.mkv", [0.0, 0.25, 0.6]),
    ("mjpeg_vbr.avi", [0.0, 0.17, 0.33]),
    ("v_vfr_gap.mp4", [0.0, 0.36, 0.37, 0.8, 1.399, 1.4, 1.75, 1.8]),
    ("still.png", [0.0, 0.5]),
    ("anim.gif", [0.0, 0.15, 0.39, 2.0]),
]

# Archivos que no son medios (se escriben tal cual) y rutas que no existen.
PROBE_RAW = [
    ("empty.mp4", b""),
    ("garbage.mp4", bytes((i * 73 + 41) % 256 for i in range(4096))),
    ("text.mp3", "esto no es audio\n".encode()),
]
PROBE_MISSING = ["no_existe.mp4", "carpeta_que_no_existe/x.wav"]


# _ext_from_magic: firmas completas, cortadas y casi iguales.
MAGIC = [
    b"", b"\x89PNG\r\n\x1a\n", b"\x89PNG\r\n\x1a", b"\x89PNG\r\n\x1a\nresto", b"\xff\xd8\xff", b"\xff\xd8\xffe0",
    b"\xff\xd8", b"\xff\xd9\xff", b"GIF87a", b"GIF89a....", b"GIF8", b"GIF88a", b"RIFF\x00\x00\x00\x00WEBP",
    b"RIFF\x00\x00\x00\x00WEBPVP8 ", b"RIFF\x00\x00\x00\x00WEB", b"RIFF\x00\x00\x00\x00WAVE", b"RIFX\x00\x00\x00\x00WEBP",
    b"\x00\x00\x00\x18ftypavif", b"BM", b"<svg", b"GIF89a\x89PNG",
]
