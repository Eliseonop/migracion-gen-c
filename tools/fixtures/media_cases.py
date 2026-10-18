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
# Otros códecs de tus vídeos: AV1 (uno de Snowpiercer; el FFmpeg de vcpkg lo decodifica con
# dav1d, como el del PATH) y HEVC (4 vídeos).
PROBE_MEDIA_CODECS = [
    ("v_av1.mp4", _V + ["-t", "0.6", "-c:v", "libaom-av1", "-cpu-used", "8", "-crf", "30", "-pix_fmt", "yuv420p"]),
    ("v_hevc.mp4", _V + ["-t", "0.6", "-c:v", "libx265", "-x265-params", "log-level=error", "-pix_fmt", "yuv420p",
                         "-tag:v", "hvc1"]),
]
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
    ("mp4 av1", "v_av1.mp4", {"kind": "video", "in_point": 0.1, "out_point": 0.6}, 30),
    ("mp4 hevc", "v_hevc.mp4", {"kind": "video", "in_point": 0.0, "out_point": 0.5}, 24),
]

# Audio del export ejecutado (gc_media/audio.hpp): material con sonido que cambia de nivel, para que
# el volumen, los fundidos, la mezcla y loudnorm se noten. Se generan UNA vez (git-lfs).
AUDIO_MEDIA = [
    # Estéreo a 44,1 kHz (el grafo lo pasa a 48 kHz): 440 Hz a la izquierda y 660 a la derecha,
    # con el nivel subiendo y bajando.
    ("tones44.wav", ["-f", "lavfi", "-i",
                     "aevalsrc=0.4*sin(2*PI*440*t)*(0.55+0.45*sin(1.3*t))|0.25*sin(2*PI*660*t)*(0.6+0.4*cos(0.7*t)):"
                     "s=44100:d=8", "-c:a", "pcm_s16le"]),
    # MP3 mono a 22,05 kHz (cambio de canales y frecuencia; el MP3 lleva relleno al principio).
    ("voice22.mp3", ["-f", "lavfi", "-i", "aevalsrc=0.5*sin(2*PI*(200+80*sin(3*t))*t)*abs(sin(2.1*t)):s=22050:d=8",
                     "-c:a", "libmp3lame", "-b:a", "96k"]),
    # Vídeo con AAC estéreo a 48 kHz (el audio de un clip de vídeo).
    ("va8.mp4", ["-f", "lavfi", "-i", "testsrc2=size=64x48:rate=25", "-f", "lavfi", "-i",
                 "aevalsrc=0.3*sin(2*PI*330*t)*(1+sin(0.9*t))/2|0.3*sin(2*PI*550*t):s=48000:d=8", "-t", "8",
                 "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "128k"]),
]
_AT = [{"id": "V1", "kind": "video", "name": "V1"}, {"id": "A1", "kind": "audio", "name": "A1"},
       {"id": "A2", "kind": "audio", "name": "A2"}]
_AV = {"kind": "video", "asset_kind": "clips", "filename": "va8.mp4", "layout": "overlay",
       "reframe": {"crop_w": 1.0, "crop_h": 1.0}, "transform": {"x": 0.5, "y": 0.5, "scale": 1.0}}
_AA = {"kind": "audio", "asset_kind": "audio"}
# (nombre, clips, objetivo de sonoridad o None). Las timelines son de 360×640 a 30 fps.
AUDIO_MIX = [
    ("un tono", [{**_AA, "track_id": "A1", "filename": "tones44.wav", "start": 0, "in_point": 0.5, "out_point": 4}],
     None),
    ("vídeo con su audio y música más baja", [
        {**_AV, "track_id": "V1", "start": 0, "in_point": 1, "out_point": 5},
        {**_AA, "track_id": "A1", "filename": "tones44.wav", "start": 0.5, "in_point": 0, "out_point": 6, "volume": 0.3}],
     None),
    ("voz con fundidos de volumen y eco", [
        {**_AA, "track_id": "A1", "filename": "voice22.mp3", "start": 0.25, "in_point": 1, "out_point": 6.5,
         "audio_fx": {"echo": 0.5},
         "keyframes": {"enabled": True, "items": [{"t": 0, "props": {"volume": 0}},
                                                  {"t": 1, "interpolation": "ease-in", "props": {"volume": 1}},
                                                  {"t": 4.5, "props": {"volume": 1}},
                                                  {"t": 5.25, "interpolation": "bezier", "props": {"volume": 0}}]}}],
     None),
    ("tres a la vez con velocidad, revés y filtros", [
        {**_AV, "track_id": "V1", "start": 0, "in_point": 0, "out_point": 3, "speed": 1.5},
        {**_AA, "track_id": "A1", "filename": "tones44.wav", "start": 1, "in_point": 2, "out_point": 6, "reverse": True,
         "audio_fx": {"underwater": 0.6, "compressor": 1}},
        {**_AA, "track_id": "A2", "filename": "voice22.mp3", "start": 2.3, "in_point": 0, "out_point": 3,
         "speed": 0.8, "keep_pitch": False, "audio_fx": {"radio": 1, "eq": True}, "volume": 1.6}],
     None),
    ("objetivo de −23 LUFS", [
        {**_AA, "track_id": "A1", "filename": "voice22.mp3", "start": 0, "in_point": 0, "out_point": 5}], -23.0),
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
    ("v_av1.mp4", [0.0, 0.25, 0.58]),
    ("v_hevc.mp4", [0.0, 0.3, 0.56]),
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
    # Las ondas en vertical (9:16): en relleno cubren el cuadro de 360×640 sin recorte (al doble).
    ("waves_tall.png", ["-f", "lavfi", "-i",
                        "nullsrc=s=180x320,format=rgb24,geq=r='128+110*sin(Y*0.35)':"
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
_MOVING6 = _MOVING.replace(":d=2,", ":d=6,")
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
    # Para el salto previo de ClipFrames (seek a un fotograma clave antes del in_point): 6 s con un
    # fotograma clave cada 0,5 s. H.264 con B-frames, H.264 de GOP abierto a 25 fps, VP9 en WebM
    # (libvpx-vp9, como el export) y un MKV que no empieza en 0.
    ("keys30.mp4", ["-f", "lavfi", "-i", _MOVING6.format(r=30), "-c:v", "libx264", "-crf", "18", "-g", "15",
                    "-pix_fmt", "yuv420p"]),
    ("keys25_open.mp4", ["-f", "lavfi", "-i", _MOVING6.format(r=25), "-c:v", "libx264", "-crf", "18", "-g", "10",
                         "-x264-params", "open-gop=1", "-pix_fmt", "yuv420p"]),
    ("keys30.webm", ["-f", "lavfi", "-i", _MOVING6.format(r=30), "-c:v", "libvpx-vp9", "-crf", "30", "-b:v", "0",
                     "-g", "15", "-deadline", "realtime", "-cpu-used", "8"]),
    ("keys30_offset.mkv", ["-f", "lavfi", "-i", _MOVING6.format(r=30), "-c:v", "libx264", "-crf", "18", "-g", "15",
                           "-pix_fmt", "yuv420p", "-output_ts_offset", "1.5"]),
]
# Escenas con varios clips o con vídeo: (nombre, fps de la timeline, pistas de vídeo, clips,
# instantes en la rejilla de 1/fps). Cada clip: archivo, pista y los campos del clip. Posiciones
# donde el redondeo del export no mueve nada (ver RENDER).
_V = {"kind": "video", "asset_kind": "clips", "layout": "overlay", "reframe": _FULL}
_I = {"kind": "image", "asset_kind": "images", "layout": "overlay", "reframe": _FULL}
_ADJ = {"kind": "adjustment", "asset_kind": "adjustment"}
_CENTER = {"x": 0.5, "y": 0.5, "scale": 1.0}
# Máscaras (clip_mask.py): el clip de ondas cubre todo el cuadro (320×180 a escala 3,2 sobre
# 360×640) encima del degradado, así se ve la forma entera. (nombre, máscaras, campos, instantes).
_COVER = {"x": 0.5, "y": 0.5, "scale": 3.2}
_SHP = {"kind": "shape", "asset_kind": "shape"}
_SHAPE_SCENES = [
    ("rectángulo redondeado con trazo", {"shape": {"type": "rect", "cornerRadius": 0.2, "x": 0.5, "y": 0.4, "w": 0.6,
                                                   "h": 0.2, "fill": "#e53935", "stroke": "#ffffff", "strokeWidth": 6}},
     [0.5]),
    ("círculo sin trazo", {"shape": {"type": "circle", "x": 0.4, "y": 0.6, "w": 0.5, "h": 0.3, "fill": "#22c55e",
                                     "strokeWidth": 0}}, [0.5]),
    ("estrella girada", {"shape": {"type": "star", "sides": 5, "x": 0.5, "y": 0.5, "w": 0.6, "h": 0.35,
                                   "rotation": 15, "fill": "#ffd400", "stroke": "#202020", "strokeWidth": 5}}, [0.5]),
    ("flecha rellena girada", {"shape": {"type": "arrow", "x": 0.5, "y": 0.45, "w": 0.7, "h": 0.15, "rotation": -30,
                                         "fill": "#3b82f6", "stroke": "#ffffff", "strokeWidth": 4}}, [0.5]),
    ("flecha curva solo contorno", {"shape": {"type": "arrow_curve", "arrowHead": "line", "x": 0.5, "y": 0.4,
                                              "w": 0.6, "h": 0.3, "stroke": "#ffffff", "strokeWidth": 5}}, [0.5]),
    ("corazón al 60 % con trazo", {"shape": {"type": "heart", "x": 0.5, "y": 0.5, "w": 0.5, "h": 0.3, "opacity": 0.6,
                                             "fill": "#e11d48", "stroke": "#ffffff", "strokeWidth": 8}}, [0.5]),
    ("check a medio dibujar", {"shape": {"type": "check", "x": 0.5, "y": 0.5, "w": 0.5, "h": 0.3, "draw": 0.6,
                                         "stroke": "#22c55e", "strokeWidth": 10}}, [0.5]),
    # Solo trazo: con relleno, el export lo oscurece al medio aparecer (mezcla con negro y baja el
    # alfa a la vez; divergencia apuntada en las notas de F3).
    ("contorno grueso a medio dibujar", {"shape": {"type": "rect", "x": 0.5, "y": 0.5, "w": 0.7, "h": 0.3, "draw": 0.4,
                                                   "fill": "none", "stroke": "#ffffff", "strokeWidth": 20}}, [0.5]),
    ("x con guiones", {"shape": {"type": "x", "x": 0.5, "y": 0.5, "w": 0.5, "h": 0.3, "dash": "dash",
                                 "stroke": "#ef4444", "strokeWidth": 6}}, [0.5]),
    ("línea de puntos", {"shape": {"type": "line", "x": 0.5, "y": 0.5, "w": 0.8, "h": 0.06, "dash": "dot",
                                   "stroke": "#ffffff", "strokeWidth": 8}}, [0.5]),
    ("trazado cerrado y suave", {"shape": {"type": "path", "x": 0.5, "y": 0.5, "w": 0.7, "h": 0.4, "closed": True,
                                           "points": [[0, 50], [30, 0], [100, 30], [70, 100], [10, 90]],
                                           "fill": "#a855f7", "stroke": "#ffffff", "strokeWidth": 4}}, [0.5]),
    ("barras de cine", {"shape": {"type": "letterbox", "bar": 0.15}}, [0.5]),
    ("bocadillo volteado", {"shape": {"type": "speech", "x": 0.5, "y": 0.4, "w": 0.6, "h": 0.3, "fill": "#ffffff",
                                      "stroke": "#111111", "strokeWidth": 4}, "flip_h": True}, [0.5]),
    ("polígono con keyframes de posición y giro",
     {"shape": {"type": "polygon", "sides": 6, "w": 0.4, "h": 0.22, "fill": "#f97316", "stroke": "#ffffff",
                "strokeWidth": 4},
      "keyframes": {"enabled": True, "items": [{"t": 0, "props": {"x": 0.3, "y": 0.4, "rotation": 0}},
                                               {"t": 1, "props": {"x": 0.7, "y": 0.6, "rotation": 90}}]}},
     [0.0, 0.5, 1.0]),
    ("rectángulo con fundido de entrada", {"shape": {"type": "rect", "x": 0.5, "y": 0.5, "w": 0.6, "h": 0.2,
                                                     "fill": "#0ea5e9", "stroke": "#ffffff", "strokeWidth": 4},
                                           "appear": "fade"}, [0.2]),
    ("figura multiplicada", {"shape": {"type": "circle", "x": 0.5, "y": 0.5, "w": 0.6, "h": 0.35, "fill": "#808080",
                                       "strokeWidth": 0}, "blend_mode": "multiply"}, [0.5]),
    ("figura con máscara", {"shape": {"type": "rect", "x": 0.5, "y": 0.5, "w": 0.8, "h": 0.4, "fill": "#fde047",
                                      "strokeWidth": 0},
                            "masks": [{"type": "circle", "x": 0.5, "y": 0.5, "w": 0.5, "h": 0.3}]}, [0.5]),
]
# Efectos de clip sobre un clip de 320×180 en el centro del cuadro: (nombre, archivo, campos, instantes).
_EFFECT_SCENES = [
    ("desenfoque 2", "waves.png", {"effects": {"blur": 2}}, [0.5]),
    ("desenfoque 6,5 en un png con alfa (también se desenfoca el alfa)", "alpha.png", {"effects": {"blur": 6.5}},
     [0.5]),
    ("nitidez 1,5", "waves.png", {"effects": {"sharpen": 1.5}}, [0.5]),
    ("blanco y negro", "waves.png", {"effects": {"grayscale": True}}, [0.5]),
    ("sepia", "waves.png", {"effects": {"sepia": True}}, [0.5]),
    ("VHS", "waves.png", {"effects": {"vhs": True}}, [0.5]),
    ("brillo, contraste y saturación", "waves.png",
     {"effects": {"brightness": 0.15, "contrast": -0.2, "saturation": 0.4}}, [0.5]),
    ("contraste 10 (la tabla de eq)", "waves.png", {"effects": {"contrast": 9}}, [0.5]),
    ("exposición, temperatura y tono", "waves.png", {"effects": {"exposure": 0.3, "temperature": 0.5, "hue": 20}},
     [0.5]),
    ("desenfoque, nitidez, sepia, brillo y blancos (el orden de la cadena)", "waves.png",
     {"effects": {"blur": 1.5, "sharpen": 2, "sepia": True, "brightness": 0.1, "whites": 0.3}}, [0.5]),
    ("efectos con un filtro de color", "waves.png",
     {"filters": [{"id": "noir", "amount": 0.6}], "effects": {"blur": 3, "contrast": 0.3}}, [0.5]),
    ("desenfoque y saturación en un vídeo con fundido de entrada", "moving30.mp4",
     {"appear": "fade", "effects": {"blur": 3, "saturation": -0.5}}, [0.2, 1.0]),
    ("VHS y desenfoque en un clip al 60 %", "waves.png", {"opacity": 0.6, "effects": {"vhs": True, "blur": 1}}, [0.5]),
    ("desenfoque y brillo con máscara de ajuste", "waves.png",
     {"effects": {"blur": 2, "brightness": 0.3},
      "masks": [{"type": "rectangle", "target": "adjust", "x": 0.5, "y": 0.5, "w": 0.4, "h": 0.2}]}, [0.5]),
    ("grano 12", "grad.png", {"effects": {"grain": 12}}, [0.5]),
]
# Máscaras de tipo texto (_paint_text de clip_mask.py, con Pillow): (nombre, máscara cruda, ancho, alto).
_TM = {"type": "text", "x": 0.5, "y": 0.5}
TEXT_MASKS = [
    ("por defecto (TEXTO en Anton)", {**_TM}, 360, 640),
    ("Arial Black, dos líneas", {**_TM, "text": {"content": "HOLA\nmundo", "font": "Arial Black", "size": 0.12}}, 360, 640),
    ("alineada a la izquierda", {**_TM, "w": 0.4, "text": {"content": "Izq\nlínea dos", "align": "left", "size": 0.1}},
     360, 640),
    ("alineada a la derecha y escalada", {**_TM, "w": 0.3, "scale_x": 1.5,
                                          "text": {"content": "Der\nmás larga", "align": "right", "size": 0.1}}, 360, 640),
    ("peso 400 (sin negrita): Arial", {**_TM, "text": {"content": "Arial fina", "font": "Arial", "weight": 400,
                                                        "size": 0.08}}, 360, 640),
    ("girada 25°", {**_TM, "rotation": 25, "text": {"content": "GIRO", "size": 0.15}}, 360, 640),
    ("girada −90° fuera del centro", {**_TM, "x": 0.3, "y": 0.6, "rotation": -90,
                                      "text": {"content": "LADO", "font": "Impact", "size": 0.1}}, 360, 640),
    ("difuminada e invertida", {**_TM, "feather": 0.01, "invert": True, "text": {"content": "BLUR", "size": 0.2}},
     360, 640),
    ("al 50 %", {**_TM, "opacity": 0.5, "text": {"content": "MEDIA", "size": 0.12}}, 360, 640),
    ("tamaño mínimo y máximo", {**_TM, "text": {"content": "chico", "size": 0.001}}, 360, 640),
    ("muy grande, se sale", {**_TM, "text": {"content": "ENORME", "size": 1.5}}, 360, 640),
    ("horizontal, Tahoma", {**_TM, "y": 0.4, "text": {"content": "Tahoma 16:9", "font": "Tahoma", "size": 0.2}},
     640, 360),
    ("con tildes y ñ", {**_TM, "text": {"content": "Añoró Ñandú", "font": "Segoe UI Black", "size": 0.08}}, 360, 640),
    ("líneas vacías", {**_TM, "text": {"content": "\nmedio\n", "size": 0.1}}, 360, 640),
    ("fuente que no existe (la de respaldo)", {**_TM, "text": {"content": "Nada", "font": "No Existe", "size": 0.1}},
     360, 640),
]
# Medidas de Pillow (font_metrics y anclas de ImageDraw.text) de los textos de las máscaras:
# (fuente, negrita, tamaño en px, texto).
TEXT_MASK_METRICS = [(f, b, s, t) for f, b in [("Anton", True), ("Arial Black", True), ("Arial", False),
                                                ("Impact", False), ("Tahoma", True), ("Segoe UI Black", True)]
                     for s in (2, 11, 12, 37, 80, 141) for t in ("TEXTO", "Hola mundo", "Añoró Ñandú", "AVAV fi", "")]

# Escenas con grano (aleatorio: no es el mismo que el del export): se comparan las medias por
# bloques de 8×8 y la fuerza del ruido, no los píxeles.
RENDER_NOISY = {"efecto: grano 12"}
# La cadena de FFmpeg de effects_ffmpeg (clip_fx.py) para estos `effects`, en un cuadro de 360×640:
# qué lee el export de cada valor (texto, True, límites, redondeos).
EFFECT_CHAINS = [
    {}, {"blur": 0}, {"blur": 2.5}, {"blur": "2"}, {"blur": True}, {"blur": 25}, {"blur": "abc"}, {"blur": None},
    {"blur": -1}, {"blur": 1.005}, {"blur": 2.675}, {"blur": " 3 "}, {"blur": [1]},
    {"sharpen": 1.5}, {"sharpen": 7}, {"sharpen": 0.125},
    {"grayscale": True}, {"grayscale": 0.5}, {"grayscale": "1"}, {"grayscale": "x"}, {"grayscale": 0},
    {"grayscale": False}, {"grayscale": -1},
    {"sepia": True}, {"vhs": 1}, {"vhs": "0"},
    {"grain": 12}, {"grain": 12.5}, {"grain": 13.5}, {"grain": 0.4}, {"grain": 80}, {"grain": True},
    {"pixelate": 1}, {"pixelate": 1.99}, {"pixelate": 2}, {"pixelate": 2.5}, {"pixelate": 3.5}, {"pixelate": 50},
    {"pixelate": "8"},
    {"brightness": 0.1}, {"brightness": -0.25}, {"brightness": 0.0004}, {"brightness": 0.0005}, {"brightness": 2},
    {"contrast": 0.2}, {"contrast": -0.5}, {"contrast": 2000}, {"contrast": -3},
    {"saturation": -1}, {"saturation": -1.5}, {"saturation": 0.3}, {"saturation": 5},
    {"exposure": 0.5}, {"temperature": -0.3, "hue": 45}, {"whites": 0.4}, {"whites": -0.7, "exposure": -2},
    {"blur": 1, "sharpen": 1, "grayscale": True, "sepia": True, "vhs": True, "grain": 5, "pixelate": 4,
     "brightness": 0.1, "contrast": 0.1, "saturation": 0.1, "exposure": 0.2},
    {"glow": 6},
    {"effects": {"exposure": 0.5}, "blur": 1},
]
_BLEND_MODES = ["darken", "multiply", "color_burn", "lighten", "screen", "color_dodge", "overlay", "soft_light",
                "hard_light", "difference", "exclusion"]
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
    # Modos de fusión (clip_blend.py): las ondas fundidas con el degradado de debajo. El export las
    # pone en una capa transparente del tamaño del cuadro y las funde con blend (o lut2 en la luz
    # suave); el render, con una capa de Skia con su modo (fórmulas W3C en los dos).
    *[(f"fusión: {mode}", 30, ["V1", "V2"],
       [("grad.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _COVER}),
        ("waves.png", "V2", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _CENTER,
                             "blend_mode": mode})],
       [0.5]) for mode in _BLEND_MODES],
    ("fusión superponer al 60 %", 30, ["V1", "V2"],
     [("grad.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _COVER}),
      ("waves.png", "V2", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _CENTER,
                           "blend_mode": "overlay", "opacity": 0.6})],
     [0.5]),
    ("fusión multiplicar con un png con alfa", 30, ["V1", "V2"],
     [("waves.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _COVER}),
      ("alpha.png", "V2", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _CENTER,
                           "blend_mode": "multiply"})],
     [0.5]),
    ("fusión trama con una máscara", 30, ["V1", "V2"],
     [("grad.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _COVER}),
      ("waves.png", "V2", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _COVER,
                           "blend_mode": "screen",
                           "masks": [{"type": "circle", "x": 0.5, "y": 0.5, "w": 0.6, "h": 0.4, "feather": 0.02}]})],
     [0.5]),
    ("fusión diferencia en un vídeo con fundido", 30, ["V1", "V2"],
     [("waves.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _COVER}),
      ("moving30.mp4", "V2", {**_V, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _CENTER,
                              "blend_mode": "difference", "appear": "fade"})],
     [0.2, 1.0]),
    # Capa de ajuste (compose.py, #19): adjustment_matrix con colorchannelmixer sobre lo compuesto
    # debajo durante su tramo; el render, la misma matriz sobre lo ya dibujado.
    ("capa de ajuste: noir sobre dos clips", 30, ["V1", "V2", "V3"],
     [("waves.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _COVER}),
      ("moving30.mp4", "V2", {**_V, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _CENTER}),
      ("", "V3", {**_ADJ, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "filters": [{"id": "noir"}]})],
     [0.5]),
    ("capa de ajuste: brillo, contraste, saturación y color", 30, ["V1", "V2"],
     [("grad.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _COVER}),
      ("", "V2", {**_ADJ, "start": 0.0, "in_point": 0.0, "out_point": 2.0,
                  "effects": {"brightness": 0.1, "contrast": 0.25, "saturation": -0.4, "exposure": 0.3,
                              "temperature": 0.5, "hue": 25}})],
     [0.5]),
    ("capa de ajuste al 50 % entre dos clips", 30, ["V1", "V2", "V3"],
     [("waves.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _COVER}),
      ("", "V2", {**_ADJ, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "opacity": 0.5,
                  "filters": [{"id": "teal_orange"}, {"id": "faded", "amount": 0.7}]}),
      ("grad.png", "V3", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _CENTER})],
     [0.5]),
    ("capa de ajuste en un tramo", 30, ["V1", "V2"],
     [("moving30.mp4", "V1", {**_V, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _COVER}),
      ("", "V2", {**_ADJ, "start": 0.5, "in_point": 0.0, "out_point": 1.0, "filters": [{"id": "night"}]})],
     [0.2, 1.0, 1.8]),
    # Figuras (shapes.py): el export las rasteriza a un PNG del cuadro con OpenCV (al doble y con
    # INTER_AREA); el render las dibuja con Skia en la caja de su pose.
    *[(f"figura: {name}", 30, ["V1", "V2"],
       [("grad.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _COVER}),
        ("", "V2", {**_SHP, "start": 0.0, "in_point": 0.0, "out_point": 2.0, **fields})],
       times) for name, fields, times in _SHAPE_SCENES],
    # Efectos de clip (effects_ffmpeg de clip_fx.py): el export los aplica al clip ya escalado (en
    # overlay, a su destino; en relleno, al cuadro), tras el `look` y antes del fundido de
    # aparición; el render, sobre una imagen del tamaño del destino (effects.hpp).
    *[(f"efecto: {name}", 30, ["V1", "V2"],
       [("grad.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _COVER}),
        (file, "V2", {**(_V if file.endswith(".mp4") else _I), "start": 0.0, "in_point": 0.0, "out_point": 2.0,
                      "transform": _CENTER, **fields})],
       times) for name, file, fields, times in _EFFECT_SCENES],
    # El pixelado, en relleno: en overlay el export lo estira al cuadro entero (scale=W:H; un fallo
    # suyo, el render vuelve al tamaño del clip). Con las ondas en vertical, que cubren el cuadro
    # sin recortar: con un recorte, el export lo redondea a píxeles pares (de 320×180, 100 px de
    # ancho en vez de 101,25) y el pixelado, que toma un solo píxel por bloque, lo agranda (36 dB).
    ("efecto: pixelado 8 en relleno", 30, ["V1"],
     [("waves_tall.png", "V1", {"kind": "image", "asset_kind": "images", "layout": "fill", "start": 0.0, "in_point": 0.0,
                           "out_point": 2.0, "effects": {"pixelate": 8}})],
     [0.5]),
    ("efecto: figura con desenfoque", 30, ["V1", "V2"],
     [("grad.png", "V1", {**_I, "start": 0.0, "in_point": 0.0, "out_point": 2.0, "transform": _COVER}),
      ("", "V2", {**_SHP, "start": 0.0, "in_point": 0.0, "out_point": 2.0,
                  "shape": {"type": "star", "sides": 5, "x": 0.5, "y": 0.5, "w": 0.6, "h": 0.35, "fill": "#ffd400",
                            "stroke": "#202020", "strokeWidth": 5},
                  "effects": {"blur": 3, "saturation": 0.5}})],
     [0.5]),
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
# Keyframes de texto: posición, escala y giro; y opacidad.
_KF_MOVE = {"enabled": True, "items": [{"t": 0, "props": {"x": 0.3, "y": 0.4, "scale": 1, "rotation": 0}},
                                       {"t": 1.5, "props": {"x": 0.7, "y": 0.6, "scale": 1.4, "rotation": 30}}]}
_KF_FADE = {"enabled": True, "items": [{"t": 0, "props": {"opacity": 0}}, {"t": 1, "props": {"opacity": 1}}]}
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
    # Etapa 2c: giro y volteo (\org en el centro de la caja, \frz, \frx180 / \fry180).
    ("girado 15°", None, [(_HOLA, {**_PLAIN, "rotation": 15}, {})], [0.5]),
    ("girado −30° y alineado a la izquierda (gira sobre el centro de la caja)", None,
     [(_HOLA + "\nuna más", {**_PLAIN, "align": "left", "w": 0.7, "rotation": -30}, {})], [0.5]),
    ("volteo horizontal", None, [(_HOLA, _PLAIN, {"flip_h": True})], [0.5]),
    ("volteo vertical y girado 10°", None, [(_HOLA, {**_PLAIN, "rotation": 10}, {"flip_v": True})], [0.5]),
    ("los dos volteos", None, [(_HOLA, _PLAIN, {"flip_h": True, "flip_v": True})], [0.5]),
    ("girado con sombra", None,
     [(_HOLA, {**_PLAIN, "rotation": 20, "shadow": True, "shadow_distance": 0.08}, {})], [0.5]),
    ("girado con Fondo por línea", None,
     [(_HOLA, {**_PLAIN, "rotation": 12, "bg": "#2040a0", "bg_opacity": 0.9, "border_width": 3}, {})], [0.5]),
    ("girado con Fondo dibujado", None,
     [(_HOLA, {**_PLAIN, "rotation": 12, "bg": "#303030", "bg_opacity": 0.85, "bg_style": "block", "bg_radius": 0.3,
               "border_width": 0}, {})], [0.5]),
    ("aparición deslizar volteada en vertical", None,
     [(_HOLA, {**_PLAIN, "block_appear": "slide_up"}, {"flip_v": True})], [0.1]),
    # Texto animado: un evento por fotograma con su pose y la aparición horneada (animated_dialogues).
    ("animado: posición, escala y giro por keyframes", None, [(_HOLA, _PLAIN, {"keyframes": _KF_MOVE})],
     [0.0, 0.5, 1.0, 1.9]),
    ("animado: opacidad por keyframes", None, [(_HOLA, _PLAIN, {"keyframes": _KF_FADE})], [0.7]),
    ("animado: opacidad por keyframes, por debajo de 0,5", None, [(_HOLA, _PLAIN, {"keyframes": _KF_FADE})], [0.3]),
    ("animado con aparición pop", None, [(_HOLA, {**_PLAIN, "block_appear": "pop"}, {"keyframes": _KF_MOVE})],
     [0.3]),
    ("animado con aparición pop, al principio (opacidad 0,25)", None,
     [(_HOLA, {**_PLAIN, "block_appear": "pop"}, {"keyframes": _KF_MOVE})], [0.1]),
    ("animado con aparición deslizar", None,
     [(_HOLA, {**_PLAIN, "block_appear": "slide_up"}, {"keyframes": _KF_MOVE})], [0.1]),
    ("animado con aparición fundida y karaoke", None, [(_HOLA, _CLASSIC_TXT, {"keyframes": _KF_MOVE})], [0.7]),
    ("animado con aparición fundida, al principio (opacidad 0,25)", None,
     [(_HOLA, _CLASSIC_TXT, {"keyframes": _KF_MOVE})], [0.1]),
    ("animado con sombra", None,
     [(_HOLA, {**_PLAIN, "shadow": True, "shadow_color": "#ff0000", "shadow_distance": 0.08},
       {"keyframes": _KF_MOVE})], [0.5]),
    ("animado con Fondo (dibujado como forma)", None,
     [(_HOLA, {**_PLAIN, "bg": "#2040a0", "bg_opacity": 0.9, "border_width": 3}, {"keyframes": _KF_MOVE})], [0.5]),
    ("animado con «escribir»", None, [(_HOLA, {**_PLAIN, "block_appear": "typing"}, {"keyframes": _KF_MOVE})],
     [0.5, 1.0]),
    ("animado y volteado", None, [(_HOLA, _PLAIN, {"keyframes": _KF_MOVE, "flip_h": True})], [0.5]),
    # La Curva: una letra por evento, centrada en su punto del arco y girada sobre sí misma.
    ("Curva hacia arriba", None, [(_HOLA, {**_PLAIN, "curve_on": True, "curve": 0.6}, {})], [0.5]),
    ("Curva hacia abajo en dos líneas", None,
     [(_HOLA + "\nsegunda línea", {**_PLAIN, "curve_on": True, "curve": -0.5}, {})], [0.5]),
    ("Curva girada 15° con sombra", None,
     [(_HOLA, {**_PLAIN, "curve_on": True, "curve": 0.5, "rotation": 15, "shadow": True, "shadow_distance": 0.08},
       {})], [0.5]),
    ("Curva con karaoke", None,
     [(_HOLA, {**_CLASSIC_TXT, "curve_on": True, "curve": 0.7, "block_appear": "none"}, {})], [0.7]),
    ("Curva con aparición deslizar", None,
     [(_HOLA, {**_PLAIN, "curve_on": True, "curve": 0.6, "block_appear": "slide_up"}, {})], [0.1]),
    ("Curva animada", None, [(_HOLA, {**_PLAIN, "curve_on": True, "curve": 0.6}, {"keyframes": _KF_MOVE})],
     [0.5, 1.0]),
    ("Curva animada con sombra", None,
     [(_HOLA, {**_PLAIN, "curve_on": True, "curve": -0.6, "shadow": True, "shadow_color": "#ff0000",
               "shadow_distance": 0.08}, {"keyframes": _KF_MOVE})], [0.5]),
    # Capa propia (_prepare_texts): giro 3D (la capa deformada con `perspective`), máscara y fusión.
    ("3D: girado en Y 35°", None, [(_HOLA, {**_PLAIN, "rot_y": 35, "size": 0.07}, {})], [0.5]),
    ("3D: inclinado en X −40° con perspectiva 1 y dos líneas", None,
     [(_HOLA + "\nsegunda línea", {**_PLAIN, "rot_x": -40, "perspective": 1, "size": 0.06}, {})], [0.5]),
    ("3D: girado en X e Y con Fondo", None,
     [(_HOLA, {**_PLAIN, "rot_x": 25, "rot_y": -30, "bg": "#2040a0", "bg_opacity": 1, "border_width": 3}, {})],
     [0.5]),
    ("3D con sombra", None,
     [(_HOLA, {**_PLAIN, "rot_y": -30, "shadow": True, "shadow_distance": 0.08, "shadow_opacity": 0.8}, {})],
     [0.5]),
    ("3D: girado en el plano y en Y, a un lado", None,
     [(_HOLA, {**_PLAIN, "rot_y": 50, "rotation": 15, "x": 0.35, "y": 0.3}, {})], [0.5]),
    ("3D animado: rot_x de 0 a 60 con keyframes", None,
     [(_HOLA, {**_PLAIN, "size": 0.07}, {"keyframes": {"enabled": True, "items": [
         {"t": 0, "props": {"rot_x": 0, "rot_y": -20}}, {"t": 1.5, "props": {"rot_x": 60, "rot_y": 20}}]}})],
     [0.0, 0.5, 1.2]),
    ("3D con karaoke y aparición fundida", None, [(_HOLA, {**_CLASSIC_TXT, "rot_y": 30}, {})], [0.1, 0.7]),
    ("máscara circular difuminada en un texto", None,
     [(_HOLA, {**_PLAIN, "size": 0.08}, {"masks": [{"type": "circle", "x": 0.5, "y": 0.5, "w": 0.35, "h": 0.12,
                                                    "feather": 0.02}]})], [0.5]),
    ("máscara lineal invertida en un texto de dos líneas", None,
     [(_HOLA + "\nsegunda línea", _PLAIN, {"masks": [{"type": "linear", "x": 0.5, "y": 0.5, "rotation": 20,
                                                     "invert": True}]})], [0.5]),
    ("máscara animada con keyframes en un texto", None,
     [(_HOLA, {**_PLAIN, "size": 0.08}, {"masks": [{"type": "rectangle", "w": 0.2, "h": 0.2}],
                                         "keyframes": {"enabled": True, "items": [
                                             {"t": 0, "props": {"mx": 0.3}}, {"t": 1.5, "props": {"mx": 0.7}}]}})],
     [0.2, 1.0]),
    ("texto multiplicado sobre un vídeo", "moving30.mp4",
     [(_HOLA, {**_PLAIN, "size": 0.08, "color": "#ff8800"}, {"blend_mode": "multiply"})], [0.5]),
    ("texto en pantalla sobre un vídeo", "moving30.mp4",
     [(_HOLA, {**_PLAIN, "size": 0.08, "color": "#3366ff"}, {"blend_mode": "screen"})], [0.5]),
    ("3D, máscara y diferencia sobre un vídeo", "moving30.mp4",
     [(_HOLA, {**_PLAIN, "size": 0.08, "rot_y": 30},
       {"blend_mode": "difference", "masks": [{"type": "rectangle", "x": 0.5, "y": 0.5, "w": 0.3, "h": 0.2}]})],
     [0.5]),
    ("capa propia debajo del texto normal (el .ass global va encima)", None,
     [(_HOLA, {**_PLAIN, "size": 0.08, "color": "#ff3030", "rot_y": 25}, {}),
      (_HOLA, {**_PLAIN, "size": 0.08, "y": 0.52}, {})], [0.5]),
    ("dos en capa propia: manda el inicio, no la lista", None,
     [("arriba", {**_PLAIN, "size": 0.1, "color": "#30ff30", "rot_x": 20}, {"start": 0.2}),
      ("abajo", {**_PLAIN, "size": 0.1, "color": "#ff30ff", "rot_x": -20, "y": 0.52}, {"start": 0.0})], [0.5]),
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
_FORCED_WHITE = ("libass lee el color de \\c y \\3c como un entero con signo y saturado: con una opacidad de "
                 "0,5 o menos (alfa ≥ 0x80), relleno y trazo salen blancos; el C++ pinta el color pedido")
_ALPHA_CUBED = ("en capa propia, el filtro ass con alpha=1 deja en la capa transparente el color premultiplicado y "
                "el alfa al cuadrado, y el overlay vuelve a multiplicar: lo semitransparente sale como c·α³ (un "
                "blanco al 50 % da 31 en vez de 128); el C++ pinta c·α, como el preview")
RENDER_TEXT_DIVERGENCES = {
    "sobre una imagen": "con una imagen PNG debajo, libass escribe el blanco como 235 (rango de TV) en un "
                        "fotograma de rango completo: el texto sale gris (sobre vídeo o negro sale 255)",
    "girado con Fondo por línea": "libass dibuja la caja de BorderStyle 4 sin girar, en el sitio de la línea sin "
                                  "giro, mientras gira el texto; el preview y el C++ la giran con él",
    "animado con aparición pop, al principio (opacidad 0,25)": _FORCED_WHITE,
    "animado: opacidad por keyframes, por debajo de 0,5": _FORCED_WHITE,
    "animado con aparición fundida, al principio (opacidad 0,25)": _FORCED_WHITE,
    "3D con karaoke y aparición fundida": _ALPHA_CUBED,
}

# Suelo del PSNR de luma estricto de algunas escenas (2 dB por debajo de lo medido, 2026-10-17): con
# solo los 16 dB de todas, el test no vería, por ejemplo, el medio píxel de la homografía del 3D.
RENDER_TEXT_FLOORS = {
    "3D: girado en Y 35°": 25,
    "3D: inclinado en X −40° con perspectiva 1 y dos líneas": 26,
    "3D: girado en X e Y con Fondo": 26,
    "3D con sombra": 25,
    "3D: girado en el plano y en Y, a un lado": 25,
    "máscara circular difuminada en un texto": 31,
    "máscara lineal invertida en un texto de dos líneas": 27,
    "máscara animada con keyframes en un texto": 26,
    "texto multiplicado sobre un vídeo": 30,
    "texto en pantalla sobre un vídeo": 35,
    "3D, máscara y diferencia sobre un vídeo": 29,
    "capa propia debajo del texto normal (el .ass global va encima)": 26,
    "dos en capa propia: manda el inicio, no la lista": 26,
}

# Escenas de RENDER_TEXT en las que el export se aparta solo un poco (decisión de Edu): el test las
# compara con el PSNR que perdona 1 px, pero no con el estricto.
RENDER_TEXT_LOOSE = {
    "3D animado: rot_x de 0 a 60 con keyframes":
        "la variable `in` de perspective empieza en 1 y _frame_expr la trata como si empezara en 0: el export "
        "deforma cada fotograma con las esquinas del siguiente; el C++ usa las del mismo fotograma que el texto",
}

# Escenas de RENDER_TEXT de una etapa siguiente, que el render aún no dibuja: si da Unsupported, el
# test las salta; si ya las dibuja, las compara.
RENDER_TEXT_PENDING: set[str] = set()


# --- Export entero (compose.render): vídeo y audio en un MP4 ----------------------------------
# El material se lee en su sitio: clips de colección con la raíz de colecciones en tests/fixtures
# (filename = ruta relativa a esa carpeta). Timelines de 360×640. Geometría entera y par en
# todos los overlays (x·W − ancho/2 y y·H − alto/2 pares), para no mezclar la divergencia del
# redondeo a pares del export (dest_rect_even) con lo que se compara.
_EV = {"kind": "video", "asset_kind": "clips", "layout": "overlay", "reframe": _FULL}
_EI = {"kind": "image", "asset_kind": "images", "layout": "overlay", "reframe": _FULL}
_EA = {"kind": "audio", "asset_kind": "audio"}
# (nombre, fps, pistas, [(archivo, pista, campos)]).
EXPORT = [
    ("vídeo, vídeo con sonido, imagen con fundido y voz", 30, ["V1", "V2", "V3", "A1"], [
        ("render/files/moving30.mp4", "V1", {**_EV, "start": 0.0, "in_point": 0.0, "out_point": 2.0,
                                             "transform": _CENTER}),
        # 64×48 a escala 2: 128×96 en (116, 112).
        ("media/files/va8.mp4", "V2", {**_EV, "start": 0.4, "in_point": 1.0, "out_point": 2.4,
                                       "transform": {"x": 0.5, "y": 0.25, "scale": 2.0}}),
        # 320×180 a escala 0,6: 192×108 en (84, 426).
        ("render/files/waves.png", "V3", {**_EI, "start": 1.0, "in_point": 0.0, "out_point": 0.8, "appear": "fade",
                                          "transform": {"x": 0.5, "y": 0.75, "scale": 0.6}}),
        ("media/files/voice22.mp3", "A1", {**_EA, "start": 0.2, "in_point": 0.5, "out_point": 2.0, "volume": 0.7}),
    ]),
    ("25 fps, vídeo con sonido a velocidad 1,5 y un tono", 25, ["V1", "A1"], [
        # 64×48 a escala 4: 256×192 en (52, 224).
        ("media/files/va8.mp4", "V1", {**_EV, "start": 0.0, "in_point": 0.0, "out_point": 3.0, "speed": 1.5,
                                       "transform": {"x": 0.5, "y": 0.5, "scale": 4.0}}),
        ("media/files/tones44.wav", "A1", {**_EA, "start": 0.5, "in_point": 0.0, "out_point": 1.5, "volume": 0.5}),
    ]),
]
