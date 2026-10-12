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
# VP9 con alfa (motion graphics): ffmpeg_input_args fuerza `-c:v libvpx-vp9` en todo .webm,
# porque el VP9 nativo no expone el plano alfa. Un círculo semitransparente que se mueve.
PROBE_MEDIA_ALPHA = [
    ("motion_alpha.webm", ["-f", "lavfi", "-i", "testsrc2=size=64x48:rate=25", "-t", "0.6", "-vf",
                           "format=yuva420p,geq=lum='lum(X,Y)':cb='cb(X,Y)':cr='cr(X,Y)':"
                           "a='if(lt(hypot(X-32-20*sin(T*6),Y-24),18),200,0)'",
                           "-c:v", "libvpx-vp9", "-pix_fmt", "yuva420p", "-auto-alt-ref", "0"]),
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
    ("webm vp9 con alfa", "motion_alpha.webm", {"kind": "video", "in_point": 0.0, "out_point": 0.6}, 30),
    ("motion vp9 con alfa", "motion_alpha.webm", {"kind": "motion", "asset_kind": "motion", "in_point": 0.1,
                                                  "out_point": 0.5}, 25),
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
    # Contenedores que no empiezan en 0 (el seek va a ss + start_time), paquetes rotos y VP9
    # con alfa (extract_frame no fuerza libvpx: decodificador nativo).
    ("v_offset.mkv", [0.0, 0.02, 0.3, 0.5, 0.52, 1.0, 1.459, 1.46, 1.5, 2.5]),
    ("va_offset.mp4", [0.0, 0.01, 0.03, 0.5, 1.2, 1.24, 1.3]),
    ("v_corrupt.mp4", [0.0, 0.5, 0.9, 1.0, 1.1, 1.5, 1.96]),
    ("motion_alpha.webm", [0.0, 0.3, 0.56, 0.6]),
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


# --- Render: fotogramas del compuesto real (compose.render_frame) ---------------------------
# Material: (archivo, argumentos de ffmpeg). Un degradado suave (las diferencias de muestreo
# entre swscale y Skia pesan poco) y una textura de ondas en X e Y (periodo de ~18 px; en el
# verde, que es lo que más pesa en la luma, en las dos direcciones) para que un error de
# posición de 1 px se note dentro del clip, no solo en el borde. Se generan UNA
# vez (van a git-lfs).
RENDER_MEDIA = [
    ("grad.png", ["-f", "lavfi", "-i", "nullsrc=s=320x180,format=rgb24,geq=r='X*255/W':g='Y*255/H':b='96+64*sin(X/20)'",
                  "-frames:v", "1"]),
    ("waves.png", ["-f", "lavfi", "-i",
                   "nullsrc=s=320x180,format=rgb24,geq=r='128+110*sin(Y*0.35)':"
                   "g='128+55*sin(X*0.35)+55*sin(Y*0.3)':b='128+110*sin((X+Y)*0.2)'", "-frames:v", "1"]),
    # Un círculo con el borde semitransparente (alfa de 255 hasta 50 px del centro y 0 a 90 px).
    ("alpha.png", ["-f", "lavfi", "-i",
                   "nullsrc=s=320x180,format=rgba,geq=r='200':g='60+X/2':b='40+Y':"
                   "a='255*max(0,min(1,(90-hypot(X-160,Y-90))/40))'", "-frames:v", "1"]),
]
# Timelines de 360×640 a 30 fps con un clip de imagen en overlay: (nombre, archivo, campos del
# clip, instantes). El fondo del export es negro. El reframe lleva crop_w/crop_h, como 218 de
# los 235 overlays de tus timelines: sin ellos, el preview recorta la fuente al aspecto de la
# salida y el export no (divergencia pendiente de decidir, notas de F3).
_FULL = {"crop_w": 1.0, "crop_h": 1.0}
RENDER = [
    ("centrado a escala 0,5", "grad.png", {"transform": {"x": 0.5, "y": 0.5, "scale": 0.5}, "reframe": _FULL}, [0.5]),
    ("desplazado a escala 0,8", "grad.png", {"transform": {"x": 0.35, "y": 0.62, "scale": 0.8}, "reframe": _FULL},
     [0.5]),
    ("girado 20°", "grad.png", {"transform": {"x": 0.5, "y": 0.45, "scale": 0.6, "rotation": 20}, "reframe": _FULL},
     [0.5]),
    ("opacidad 0,5", "grad.png", {"transform": {"x": 0.5, "y": 0.5, "scale": 0.7}, "reframe": _FULL, "opacity": 0.5},
     [0.5]),
    ("escala 1,4 que se sale", "grad.png", {"transform": {"x": 0.2, "y": 0.3, "scale": 1.4}, "reframe": _FULL}, [0.5]),
    ("recorte parcial", "grad.png",
     {"transform": {"x": 0.5, "y": 0.5, "scale": 1.0},
      "reframe": {"crop_w": 0.6, "crop_h": 0.8, "zoom": 1.0, "keyframes": [{"t": 0.0, "cx": 0.35, "cy": 0.55, "zoom": 1.0}]}},
     [0.5]),
    # Con las ondas, en posiciones donde el destino en coma flotante y el de dest_rect_even del
    # export coinciden: así un error de posición del C++ se nota dentro del clip. Donde no
    # coinciden (p. ej. x 0,41, y 0,57 a escala 0,8: el export cae 0,4 px a la derecha y 0,8 px
    # arriba) la luma baja a ~30 dB solo por el redondeo (decisión de Edu, notas de F3).
    ("ondas centradas", "waves.png", {"transform": {"x": 0.5, "y": 0.5, "scale": 1.0}, "reframe": _FULL}, [0.5]),
    ("ondas desplazadas a escala 0,8", "waves.png", {"transform": {"x": 0.4, "y": 0.55, "scale": 0.8}, "reframe": _FULL},
     [0.5]),
    # 18°: el rotate del export da un cuadro de 360 × 270 (redondea rotw/roth), así que el
    # (overlay_h − dh)/2 del overlay es entero. A 20° sale 279 de alto, el overlay trunca medio
    # píxel y la luma baja a ~35 dB solo por eso (alineado ese medio píxel, 42 dB).
    ("ondas giradas 18°", "waves.png",
     {"transform": {"x": 0.5, "y": 0.5, "scale": 1.0, "rotation": 18}, "reframe": _FULL}, [0.5]),
]

# Vídeos del render: ondas que se mueven ~1 rad por fotograma (un fotograma de más o de menos
# baja la luma muy por debajo de 35 dB),
# en H.264 de alta calidad. Se generan UNA vez (van a git-lfs).
_MOVING = ("nullsrc=s=320x180:r={r}:d=2,format=rgb24,geq=r='128+110*sin(Y*0.35-T*20)':"
           "g='128+55*sin(X*0.35+T*30)+55*sin(Y*0.3-T*25)':b='128+110*sin((X+Y)*0.2+T*15)'")
_INSIDE = "lt(hypot(X-160-40*sin(T*3),Y-90),60)"
_KEYED = ("nullsrc=s=320x180:r={r}:d={d},format=rgb24,geq="
          "r='if(" + _INSIDE + ",128+110*sin(Y*0.35-T*20),0)':"
          "g='if(" + _INSIDE + ",128+55*sin(X*0.35+T*30)+55*sin(Y*0.3-T*25),255)':"
          "b='if(" + _INSIDE + ",128+110*sin((X+Y)*0.2+T*15),0)'")
_BARS = "lt(mod(X,24),12)"
_KEYED_BARS = ("nullsrc=s=320x180:r=1:d=1,format=rgb24,geq="
               "r='if(" + _BARS + ",250,0)':g='if(" + _BARS + ",40,255)':b='if(" + _BARS + ",210,0)'")
RENDER_VIDEO_MEDIA = [
    ("moving30.mp4", ["-f", "lavfi", "-i", _MOVING.format(r=30), "-c:v", "libx264", "-crf", "8", "-pix_fmt", "yuv420p"]),
    ("moving25.mp4", ["-f", "lavfi", "-i", _MOVING.format(r=25), "-c:v", "libx264", "-crf", "8", "-pix_fmt", "yuv420p"]),
    # Un GIF animado a 10 fps: el export lo pasa a la rejilla de la timeline con su fps=F final.
    ("anim10.gif", ["-f", "lavfi", "-i", _MOVING.format(r=10)]),
    # Croma: un círculo de ondas (r = 60 px) sobre verde puro, quieto y moviéndose.
    ("keyed.png", ["-f", "lavfi", "-i", _KEYED.format(r=1, d=1), "-frames:v", "1"]),
    ("keyed30.mp4", ["-f", "lavfi", "-i", _KEYED.format(r=30, d=2), "-c:v", "libx264", "-crf", "8",
                     "-pix_fmt", "yuv420p"]),
    # Barras magenta de 12 px sobre verde: bordes por todo el clip, para que la limpieza del borde
    # (desenfoque en px del alto del material) se note sobre las ondas del fondo.
    ("keyedbars.png", ["-f", "lavfi", "-i", _KEYED_BARS, "-frames:v", "1"]),
]
# Escenas con varios clips o con vídeo: (nombre, fps de la timeline, pistas de vídeo, clips,
# instantes en la rejilla de 1/fps). Cada clip: archivo, pista y los campos del clip. Posiciones
# donde el redondeo del export no mueve nada (ver RENDER).
_V = {"kind": "video", "asset_kind": "clips", "layout": "overlay", "reframe": _FULL}
_I = {"kind": "image", "asset_kind": "images", "layout": "overlay", "reframe": _FULL}
_CENTER = {"x": 0.5, "y": 0.5, "scale": 1.0}
# Máscaras (clip_mask.py): el clip de ondas cubre todo el cuadro (320×180 a escala 3,2 sobre
# 360×640) encima del degradado, así se ve la forma entera. (nombre, máscaras, campos, instantes).
_COVER = {"x": 0.5, "y": 0.5, "scale": 3.2}
_MASK_SCENES = [
    ("lineal girada y difuminada (como la de Snowpiercer)",
     [{"type": "linear", "x": 0.4958, "y": 0.5951, "w": 2.4, "h": 0.0534, "rotation": 0.44, "feather": 0.0153}], {},
     [0.5]),
    ("lineal girada 30° sin difuminar", [{"type": "linear", "x": 0.5, "y": 0.4, "rotation": 30}], {}, [0.5]),
    ("círculo girado e invertido", [{"type": "circle", "x": 0.45, "y": 0.55, "w": 0.4, "h": 0.25, "rotation": 20,
                                     "invert": True, "feather": 0.02}], {}, [0.5]),
    ("rectángulo redondeado al 60 %", [{"type": "rectangle", "x": 0.5, "y": 0.5, "w": 0.4, "h": 0.5, "radius": 0.3,
                                        "opacity": 0.6}], {}, [0.5]),
    ("estrella y corazón (intersección)",
     [{"type": "star", "x": 0.5, "y": 0.45, "w": 0.6, "h": 0.6},
      {"type": "heart", "x": 0.55, "y": 0.5, "w": 0.5, "h": 0.45, "feather": 0.01}], {}, [0.5]),
    ("rollo de película muy difuminado (a resolución reducida)",
     [{"type": "film", "x": 0.5, "y": 0.5, "h": 0.3, "rotation": -10, "feather": 0.1}], {}, [0.5]),
    ("pincel", [{"type": "brush", "x": 0.5, "y": 0.5, "brush": {"size": 0.05, "points": [
        {"x": -0.2, "y": -0.2}, {"x": 0.0, "y": -0.05}, {"x": 0.15, "y": 0.1}, {"x": -0.1, "y": 0.2, "m": 1},
        {"x": 0.1, "y": 0.25}, {"x": 0.2, "y": -0.3, "m": 1}]}}], {}, [0.5]),
    ("una desactivada y otra activa", [{"type": "rectangle", "enabled": False, "w": 0.1, "h": 0.1},
                                       {"type": "circle", "x": 0.5, "y": 0.5, "w": 0.5, "h": 0.5}], {}, [0.5]),
    ("círculo animado con keyframes", [{"type": "circle", "x": 0.3, "y": 0.5, "w": 0.4, "h": 0.4}],
     {"keyframes": {"enabled": True, "items": [{"t": 0, "props": {"mx": 0.3}}, {"t": 2, "props": {"mx": 0.7}}]}},
     [0.0, 1.0]),
]
RENDER_SCENES = [
    ("vídeo centrado", 30, ["V1"],
     [("moving30.mp4", "V1", {**_V, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _CENTER})],
     [0.0, 0.5, 1.0]),
    ("vídeo con in_point y start", 30, ["V1"],
     [("moving30.mp4", "V1", {**_V, "start": 0.2, "in_point": 0.4, "out_point": 1.6, "transform": _CENTER})],
     [0.2, 0.7]),
    ("vídeo de 25 fps en una timeline de 30", 30, ["V1"],
     [("moving25.mp4", "V1", {**_V, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _CENTER})],
     [0.5, 0.9, 1.3]),
    ("vídeo a velocidad 1,5", 30, ["V1"],
     [("moving30.mp4", "V1", {**_V, "start": 0.0, "in_point": 0.0, "out_point": 1.8, "speed": 1.5,
                               "transform": _CENTER})],
     [0.6, 1.0]),
    ("vídeo al revés", 30, ["V1"],
     [("moving30.mp4", "V1", {**_V, "start": 0.0, "in_point": 0.2, "out_point": 1.4, "reverse": True,
                               "transform": _CENTER})],
     [0.0, 0.4]),
    ("imagen debajo y vídeo encima", 30, ["V1", "V2"],
     [("waves.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0,
                           "transform": {"x": 0.5, "y": 0.3, "scale": 1.0}}),
      ("moving30.mp4", "V2", {**_V, "start": 0.0, "in_point": 0.0, "out_point": 2.0,
                              "transform": {"x": 0.5, "y": 0.4015625, "scale": 0.5}})],  # y·640 − 45 = 212, par
     [0.5]),
    ("png con alfa sobre ondas", 30, ["V1", "V2"],
     [("waves.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0,
                           "transform": {"x": 0.5, "y": 0.3, "scale": 1.0}}),
      ("alpha.png", "V2", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0,
                           "transform": {"x": 0.5, "y": 0.3, "scale": 1.0}})],
     [0.5]),
    # Aparición y salida (FX_DUR = 0,4 s): a mitad del fundido y de la cortinilla.
    ("fundido de entrada y de salida", 30, ["V1"],
     [("moving30.mp4", "V1", {**_V, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "appear": "fade",
                               "exit": "fade", "transform": _CENTER})],
     [0.2, 1.0, 1.8]),
    ("cortinilla de entrada", 30, ["V1"],
     [("moving30.mp4", "V1", {**_V, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "appear": "wipe",
                               "transform": _CENTER})],
     [0.2]),
    # El fotograma elegido (revisión): las setpts del export truncan a ticks enteros, así que con
    # un inicio fuera de la rejilla o una velocidad fraccionaria importa en qué tick cae cada uno.
    ("vídeo que empieza en 0,29", 30, ["V1"],
     [("moving30.mp4", "V1", {**_V, "start": 0.29, "in_point": 0.0, "out_point": 1.5, "transform": _CENTER})],
     [0.3, 0.5, 1.0]),
    ("vídeo que empieza en 0,717", 30, ["V1"],
     [("moving30.mp4", "V1", {**_V, "start": 0.717, "in_point": 0.1, "out_point": 1.5, "transform": _CENTER})],
     [0.7333, 1.0]),
    ("vídeo a velocidad 0,75", 30, ["V1"],
     [("moving30.mp4", "V1", {**_V, "start": 0.0, "in_point": 0.0, "out_point": 1.0, "speed": 0.75,
                               "transform": _CENTER})],
     [0.0667, 0.1667, 0.2]),
    ("vídeo a velocidad 1,5 en fotogramas impares", 30, ["V1"],
     [("moving30.mp4", "V1", {**_V, "start": 0.0, "in_point": 0.0, "out_point": 1.8, "speed": 1.5,
                               "transform": _CENTER})],
     [0.3, 0.3667]),
    ("vídeo a velocidad 2,5", 30, ["V1"],
     [("moving30.mp4", "V1", {**_V, "start": 0.0, "in_point": 0.0, "out_point": 1.9, "speed": 2.5,
                               "transform": _CENTER})],
     [0.5]),
    ("vídeo al revés a velocidad 1,5", 30, ["V1"],
     [("moving30.mp4", "V1", {**_V, "start": 0.0, "in_point": 0.0, "out_point": 1.5, "speed": 1.5, "reverse": True,
                               "transform": _CENTER})],
     [0.3]),
    ("gif de 10 fps en una timeline de 30", 30, ["V1"],
     [("anim10.gif", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _CENTER})],
     [0.0333, 0.5, 1.7]),
    # El instante (revisión): `-ss` a la salida da el fotograma más cercano (un empate, el
    # siguiente), y el último fotograma de un clip de 1 s es el 29.
    ("instantes fuera de la rejilla", 30, ["V1"],
     [("moving30.mp4", "V1", {**_V, "start": 0.0, "in_point": 0.0, "out_point": 1.0, "transform": _CENTER})],
     [0.3335, 0.35, 0.36, 0.45, 0.967]),
    # Croma (eliminar fondo por color) sobre las ondas: en el espacio del material, antes del
    # encuadre; con derrame (despill) y con limpieza del borde (alfa aparte: alphaextract).
    ("croma en una imagen", 30, ["V1", "V2"],
     [("waves.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _CENTER}),
      ("keyed.png", "V2", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _CENTER,
                           "bg_removal": {"enabled": True, "mode": "chroma", "chroma": {"enabled": True}}})],
     [0.5]),
    ("croma con derrame", 30, ["V1", "V2"],
     [("waves.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _CENTER}),
      ("keyed.png", "V2", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _CENTER,
                           "bg_removal": {"enabled": True, "mode": "chroma",
                                          "chroma": {"enabled": True, "similarity": 0.3, "spill": 0.5}}})],
     [0.5]),
    ("croma con limpieza del borde", 30, ["V1", "V2"],
     [("waves.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _CENTER}),
      ("keyedbars.png", "V2", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _CENTER,
                             "bg_removal": {"enabled": True, "mode": "chroma",
                                            "chroma": {"enabled": True, "edge": 0.2, "shrink": -0.3}}})],
     [0.5]),
    ("croma en un vídeo", 30, ["V1", "V2"],
     [("waves.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _CENTER}),
      ("keyed30.mp4", "V2", {**_V, "start": 0.0, "in_point": 0.0, "out_point": 2.0,
                             "transform": {"x": 0.5, "y": 0.5, "scale": 0.8},
                             "bg_removal": {"enabled": True, "mode": "chroma", "chroma": {"enabled": True}}})],
     [0.5, 1.0]),
    ("vídeo que empieza en −0,2", 30, ["V1"],
     [("moving30.mp4", "V1", {**_V, "start": -0.2, "in_point": 0.0, "out_point": 1.0, "transform": _CENTER})],
     [0.0, 0.3]),
    ("croma en un vídeo al revés a velocidad 1,5", 30, ["V1", "V2"],
     [("waves.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _CENTER}),
      ("keyed30.mp4", "V2", {**_V, "start": 0.0, "in_point": 0.0, "out_point": 1.5, "speed": 1.5, "reverse": True,
                             "transform": _CENTER,
                             "bg_removal": {"enabled": True, "mode": "chroma",
                                            "chroma": {"enabled": True, "spill": 0.3, "shrink": -0.3}}})],
     [0.3, 0.6]),
    # Filtros de color (clip_filters.py): el export los aplica con colorchannelmixer después de
    # escalar; el render, con la matriz sobre cada píxel ya muestreado.
    ("filtro noir en una imagen", 30, ["V1"],
     [("waves.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _CENTER,
                           "filters": [{"id": "noir", "amount": 1}]})],
     [0.5]),
    ("filtro sin desplazamiento (blanco y negro)", 30, ["V1"],
     [("waves.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _CENTER,
                           "filters": [{"id": "bw", "amount": 0.7}]})],
     [0.5]),
    ("look vintage en un vídeo", 30, ["V1"],
     [("moving30.mp4", "V1", {**_V, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _CENTER,
                              "look": "vintage"})],
     [0.5, 1.0]),
    ("pila de filtros con intensidad", 30, ["V1"],
     [("waves.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0,
                           "transform": {"x": 0.5, "y": 0.5, "scale": 0.8},
                           "filters": [{"id": "warm", "amount": 0.6}, {"id": "faded", "amount": 0.8},
                                       {"id": "teal_orange", "amount": 0.5}]})],
     [0.5]),
    ("desvaído con fundido de entrada, girado 18°", 30, ["V1", "V2"],
     [("waves.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _CENTER}),
      ("moving30.mp4", "V2", {**_V, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "appear": "fade",
                              "transform": {"x": 0.5, "y": 0.5, "scale": 1.0, "rotation": 18},
                              "filters": [{"id": "faded", "amount": 1}]})],
     [0.2, 1.0]),
    ("filtros vivo y frío sobre otro clip", 30, ["V1", "V2"],
     [("waves.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0,
                           "transform": {"x": 0.5, "y": 0.3, "scale": 1.0}, "filters": [{"id": "saturated"}]}),
      ("moving30.mp4", "V2", {**_V, "start": 0.0, "in_point": 0.0, "out_point": 2.0,
                              "transform": {"x": 0.5, "y": 0.4015625, "scale": 0.5},
                              "filters": [{"id": "cool", "amount": 1}]})],
     [0.5]),
    ("filtro sin desplazamiento en un png con alfa", 30, ["V1", "V2"],
     [("waves.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0,
                           "transform": {"x": 0.5, "y": 0.3, "scale": 1.0}}),
      ("alpha.png", "V2", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0,
                           "transform": {"x": 0.5, "y": 0.3, "scale": 1.0}, "filters": [{"id": "night"}]})],
     [0.5]),
    *[(f"máscara: {name}", 30, ["V1", "V2"],
       [("grad.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _COVER}),
        ("waves.png", "V2", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _COVER,
                             "masks": masks, **extra})],
       times) for name, masks, extra, times in _MASK_SCENES],
    # Sin el zoom (ni el pop) de aparición o salida: en un overlay estático el export escala con
    # `scale=eval=frame` pero el overlay se queda en x=ox:y=oy, así que crece desde la esquina
    # superior izquierda; el preview, desde el centro (20 dB; decisión de Edu, notas de F3).
]

# Texto (el .ass global de text_ass.py, dibujado por libass): escenas de 540×960 a 30 fps.
# Cada una: (nombre, fondo (imagen a pantalla completa) o None, [(texto, estilo, campos)], instantes).
_PLAIN = {"size": 0.048, "bold": True, "align": "center", "x": 0.5, "y": 0.5, "w": 0.88, "bg": "none",
          "shadow": False, "glow": False, "font": "Arial Black", "color": "#ffffff", "border_width": 5,
          "border_color": "#111111", "word_fx": "none", "block_appear": "none", "opacity": 1}
_CLASSIC_TXT = {**_PLAIN, "y": 0.86, "inactive_opacity": 0.55, "active_opacity": 1, "highlight_color": "#ffe566",
                "word_fx": "highlight", "block_appear": "fade"}
_HOLA = "Hola mundo que tal"
RENDER_TEXT = [
    ("texto blanco con trazo", None, [(_HOLA, _PLAIN, {})], [0.5]),
    ("dos líneas", None, [(_HOLA + "\nsegunda línea", _PLAIN, {})], [0.5]),
    ("alineado a la izquierda", None, [(_HOLA + "\nuna más", {**_PLAIN, "align": "left", "w": 0.7}, {})], [0.5]),
    ("alineado a la derecha", None, [(_HOLA + "\nuna más", {**_PLAIN, "align": "right", "w": 0.7}, {})], [0.5]),
    ("karaoke resaltado (el classic de Edu)", None, [(_HOLA, {**_CLASSIC_TXT, "block_appear": "none"}, {})],
     [0.2, 0.7, 1.9]),
    ("aparición fundida", None, [(_HOLA, _CLASSIC_TXT, {})], [0.0, 0.1, 0.3]),
    ("opacidad 0,6", None, [(_HOLA, {**_PLAIN, "opacity": 0.6}, {})], [0.5]),
    ("espaciado entre letras", None, [(_HOLA, {**_PLAIN, "letter_spacing": 0.1}, {})], [0.5]),
    ("Segoe UI Black", None, [(_HOLA, {**_PLAIN, "font": "Segoe UI Black", "border_width": 2}, {})], [0.5]),
    ("Tahoma", None, [(_HOLA, {**_PLAIN, "font": "Tahoma", "border_width": 0}, {})], [0.5]),
    ("Arial sin negrita", None, [(_HOLA, {**_PLAIN, "font": "Arial", "bold": False}, {})], [0.5]),
    ("Anton sin negrita", None, [(_HOLA, {**_PLAIN, "font": "Anton", "bold": False}, {})], [0.5]),
    ("sobre un vídeo", "moving30.mp4", [(_HOLA, _PLAIN, {})], [0.5]),
    ("sobre una imagen", "waves.png", [(_HOLA, _PLAIN, {})], [0.5]),
    ("palabra que empieza en 0,1 (el fotograma 3 llega a libass en 99 ms)", None,
     [(_HOLA, {**_CLASSIC_TXT, "block_appear": "none"},
       {"words": [{"text": w, "start": s, "end": s + 0.3} for w, s in zip(_HOLA.split(), (0.0, 0.1, 0.6, 1.2))]})],
     [0.1, 0.1334]),
    ("palabra que empieza en 3,7 (k = 111: k · (1/30) · 1000 = 3699,99…)", None,
     [(_HOLA, {**_CLASSIC_TXT, "block_appear": "none"},
       {"out_point": 4.0,
        "words": [{"text": w, "start": s, "end": s + 0.05} for w, s in zip(_HOLA.split(), (0.0, 1.0, 3.7, 3.8))]})],
     [3.7, 3.7334]),
    ("trazo de 8 px", None, [(_HOLA, {**_PLAIN, "border_width": 8}, {})], [0.5]),
    ("texto largo que se parte", None,
     [("Esto es un texto bastante largo que no cabe en una sola línea del vídeo vertical", _PLAIN, {})], [0.5]),
    ("escribir", None, [(_HOLA, {**_PLAIN, "block_appear": "typing"}, {})], [0.5, 1.0]),
    ("más grande y desplazado", None, [(_HOLA, {**_PLAIN, "size": 0.07, "x": 0.4, "y": 0.3}, {})], [0.5]),
    ("dos textos a la vez", None, [(_HOLA, _PLAIN, {}), ("arriba", {**_PLAIN, "y": 0.2, "color": "#00ff88"}, {})],
     [0.5]),
    ("relleno oscuro semitransparente con trazo blanco (ass_fix_outline)", None,
     [(_HOLA, {**_PLAIN, "color": "#202020", "border_color": "#ffffff", "border_width": 6, "opacity": 0.6}, {})],
     [0.5]),
    ("kerning fuerte (libass no lo aplica)", None,
     [("AVAVAVAV WAWAWA Tò", {**_PLAIN, "font": "Arial", "bold": False, "border_width": 2}, {})], [0.5]),
    # Etapa 2b: Fondo, sombra, Brillo y efectos de palabra.
    ("Fondo por línea (tema box de Edu, sin aparición)", None,
     [(_HOLA, {**_CLASSIC_TXT, "font": "Tahoma", "bg": "#111318", "bg_opacity": 0.72, "border_width": 0,
               "inactive_opacity": 0.45, "block_appear": "none"}, {})], [0.7]),
    ("Fondo con trazo", None, [(_HOLA, {**_PLAIN, "bg": "#2040a0", "bg_opacity": 0.9, "border_width": 4}, {})], [0.5]),
    ("Fondo en bloque redondeado", None,
     [(_HOLA + "\nsegunda línea", {**_PLAIN, "bg": "#202020", "bg_opacity": 0.8, "bg_style": "block",
                                   "bg_radius": 0.4, "bg_pad_x": 0.2, "bg_pad_y": 0.1, "border_width": 0}, {})], [0.5]),
    ("sombra paralela", None, [(_HOLA, {**_PLAIN, "shadow": True, "shadow_color": "#ff0000", "shadow_opacity": 0.8,
                                        "shadow_distance": 0.08, "shadow_angle": 45, "shadow_blur": 0.05}, {})], [0.5]),
    ("Brillo (tema neon de Edu)", None,
     [(_HOLA, {**_CLASSIC_TXT, "font": "Segoe UI Black", "glow": True, "shadow_color": "#00e5ff", "word_fx": "glow",
               "inactive_opacity": 0.74, "color": "#f4fbff", "border_width": 2, "border_color": "#063a48",
               "highlight_color": "#00f0ff", "block_appear": "none"}, {})], [0.7]),
    ("efecto de palabra pop", None, [(_HOLA, {**_CLASSIC_TXT, "word_fx": "pop", "block_appear": "none"}, {})], [0.7]),
    ("aparición pop", None, [(_HOLA, {**_PLAIN, "block_appear": "pop"}, {})], [0.0, 0.1, 0.5]),
    ("aparición deslizar", None, [(_HOLA, {**_PLAIN, "block_appear": "slide_up"}, {})], [0.0, 0.1, 0.5]),
    ("aparición pop con Fondo dibujado (la forma sí escala)", None,
     [(_HOLA, {**_PLAIN, "bg": "#303030", "bg_opacity": 0.8, "bg_style": "block", "border_width": 0,
               "block_appear": "pop"}, {})], [0.0, 0.1, 0.5]),
    ("aparición deslizar con sombra", None,
     [(_HOLA, {**_PLAIN, "shadow": True, "shadow_distance": 0.1, "block_appear": "slide_up"}, {})], [0.1]),
    ("Escala X / Y", None, [(_HOLA, {**_PLAIN, "scale_split": True, "stretch_x": 1.3, "stretch_y": 0.8}, {})], [0.5]),
    ("Fondo dibujado muy redondeado", None,
     [("Hola", {**_PLAIN, "bg": "#3050c0", "bg_opacity": 1, "bg_radius": 1, "bg_pad_x": 0.6, "bg_pad_y": 0.6,
                "border_width": 0, "size": 0.07}, {})], [0.5]),
    ("sombra lejana y nítida", None,
     [(_HOLA, {**_PLAIN, "shadow": True, "shadow_color": "#ffffff", "shadow_opacity": 1, "shadow_distance": 0.4,
               "shadow_angle": 60, "shadow_blur": 0, "border_width": 0}, {})], [0.5]),
    ("efecto de palabra brillo sin trazo (se desenfoca el relleno)", None,
     [(_HOLA, {**_CLASSIC_TXT, "word_fx": "glow", "border_width": 0, "block_appear": "none"}, {})], [0.7]),
    ("Fondo por línea con fundido", None,
     [(_HOLA, {**_PLAIN, "bg": "#a02020", "bg_opacity": 0.7, "border_width": 3, "block_appear": "fade"}, {})],
     [0.1]),
    # Números escritos como texto, con espacios (revisión): float()/int() de Python los aceptan.
    ("estilo con números como texto", None,
     [(_HOLA, {**_PLAIN, "size": " 0.06 ", "border_width": " 4 ", "w": "0.8 "}, {})], [0.5]),
]

# Croma: los dict crudos de bg_removal.chroma que se normalizan y se pasan a chroma_filters,
# chroma_alpha_ffmpeg y la cadena de _bg_source_chain, con varios altos de material.
CHROMA = [
    {"enabled": True},
    {"enabled": True, "color": "#00FF00", "similarity": 0.2, "blend": 0.1},
    {"enabled": True, "color": "#0000ff", "similarity": 0.35, "blend": 0.0, "spill": 0.5},
    {"enabled": True, "color": "0x1aB", "similarity": 0.123456789, "blend": 0.3333333, "spill": 0.00005},
    {"enabled": True, "color": "nada", "similarity": 0, "blend": 2, "spill": 1.5},
    {"enabled": True, "edge": 0.3},
    {"enabled": True, "shrink": -0.4},
    {"enabled": True, "shrink": 0.25, "edge": 0.1},
    {"enabled": True, "edge": 0.00005, "shrink": 0.00005},
    {"enabled": True, "edge": 0.002},
    {"enabled": True, "shrink": 0.001},
    {"enabled": True, "shrink": 0.0035},
    {"enabled": True, "color": "#22CC33", "spill": 0.2, "edge": 1, "shrink": -1},
    {"enabled": False, "edge": 0.3},
    {"enabled": "sí", "similarity": "0.4"},
    {},
]
CHROMA_HEIGHTS = [0, 180, 512, 1080, 2160]

# Escenas de RENDER_TEXT en las que el export se aparta de lo que pide el texto (decisión de Edu):
# el test las dibuja pero no compara.
RENDER_TEXT_DIVERGENCES = {
    "sobre una imagen": "con una imagen PNG debajo, libass escribe el blanco como 235 (rango de TV) en un "
                        "fotograma de rango completo: el texto sale gris (sobre vídeo o negro sale 255)",
}

# Escenas de RENDER_TEXT de una etapa siguiente, que el render aún no dibuja: si da Unsupported, el
# test las salta; si ya las dibuja, las compara.
RENDER_TEXT_PENDING: set[str] = set()
