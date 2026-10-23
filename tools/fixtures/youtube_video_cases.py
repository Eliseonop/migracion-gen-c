"""Casos de las calidades de un vídeo de YouTube (la parte sin red de youtube_video.py)."""
from __future__ import annotations


def fmt(fid, **kw):
    f = {"format_id": fid}
    f.update(kw)
    return f


A_M4A = fmt("140", vcodec="none", acodec="mp4a.40.2", ext="m4a", abr=129.5, filesize=3_000_000)
A_OPUS = fmt("251", vcodec="none", acodec="opus", ext="webm", abr=160, filesize=3_500_000)
A_DRC = fmt("140-drc", vcodec="none", acodec="mp4a.40.2", ext="m4a", abr=200, filesize=3_100_000)

# Un vídeo normal de YouTube: varias calidades, códecs y fps, HDR, HLS y storyboards.
YT = [
    fmt("sb0", format_note="storyboard", ext="mhtml", vcodec="none", acodec="none", width=48, height=27),
    A_M4A, A_OPUS, A_DRC,
    fmt("160", format_note="144p", vcodec="avc1.4d400c", acodec="none", width=256, height=144, fps=30,
        tbr=80, filesize=900_000),
    fmt("134", format_note="360p", vcodec="avc1.4d401e", acodec="none", width=640, height=360, fps=30,
        tbr=300, filesize=6_000_000),
    fmt("243", format_note="360p", vcodec="vp9", acodec="none", width=640, height=360, fps=30, tbr=280,
        filesize=5_000_000),
    fmt("18", format_note="360p", vcodec="avc1.42001E", acodec="mp4a.40.2", width=640, height=360, fps=30,
        tbr=500, filesize_approx=9_000_000),
    fmt("136", format_note="720p", vcodec="avc1.4d401f", acodec="none", width=1280, height=720, fps=30,
        tbr=1500, filesize=30_000_000),
    fmt("298", format_note="720p60", vcodec="avc1.4d4020", acodec="none", width=1280, height=720, fps=60,
        tbr=2500, filesize=50_000_000),
    fmt("302", format_note="720p60", vcodec="vp9", acodec="none", width=1280, height=720, fps=60, tbr=2400),
    fmt("299", format_note="1080p60", vcodec="avc1.64002a", acodec="none", width=1920, height=1080, fps=60,
        tbr=4500, filesize=95_000_000),
    fmt("337", format_note="1080p60 HDR", vcodec="vp09.02.51.10", acodec="none", width=1920, height=1080,
        fps=60, tbr=6000, dynamic_range="HDR10", filesize=150_000_000),
    fmt("hls-1080", format_note="1080p60", vcodec="avc1.64002a", acodec="none", width=1920, height=1080,
        fps=60, tbr=4600, protocol="m3u8_native"),
    fmt("401", format_note="2160p60", vcodec="av01.0.13M.08", acodec="none", width=3840, height=2160, fps=59.94,
        tbr=18000, filesize_approx=400_000_000),
    fmt("drm", format_note="2160p", vcodec="avc1", acodec="none", width=3840, height=2160, has_drm=True),
]

# Un Short vertical sin format_note (otros sitios): la calidad es el lado corto.
SHORT = [
    fmt("a", vcodec="h264", acodec="aac", width=1080, height=1920, fps="29.97", tbr="3100.5", filesize="12345678"),
    fmt("b", vcodec="h264", acodec="aac", width=720, height=1280, fps=29.5),
    fmt("c", vcodec="h264", acodec="aac", width=None, height=480, fps=30.5),
    fmt("d", vcodec="H264", acodec="aac", width="abc", height="360", fps=True),
    fmt("e", vcodec="vp9", acodec="opus", width=0, height=0),
    fmt("", vcodec="avc1", acodec="aac", width=640, height=360),         # sin format_id
]

# Solo calidades bajas: se ofrecen igual.
LOW = [fmt("1", format_note="240p", vcodec="avc1", acodec="none", height=240, filesize=1_000_000),
       fmt("2", format_note="144p", vcodec="avc1", acodec="none", height=144), A_M4A,
       fmt("sb", format_note="storyboard", ext="mhtml", vcodec="avc1", acodec="none", width=320, height=180),
       fmt("drm", format_note="300p", vcodec="avc1", acodec="none", has_drm=True)]

# format_note raros: dígitos de otros sistemas, 5 cifras, varias coincidencias, un número.
NOTES = [
    fmt("n1", format_note="١٠٨٠p", vcodec="avc1", acodec="none", width=1920, height=1080),
    fmt("n2", format_note="12345p", vcodec="avc1", acodec="none", width=10, height=10),
    fmt("n3", format_note="x99p 480p 720p", vcodec="avc1", acodec="none"),
    fmt("n4", format_note=720, vcodec="avc1", acodec="none", width=100, height=50),
    fmt("n5", format_note="𝟳𝟮𝟬p", vcodec="avc1", acodec="none"),
    fmt("n6", format_note=["1080p"], vcodec="avc1", acodec="none"),
    fmt("n7", format_note="", vcodec="avc1", acodec="none", width=-640, height=360),
    fmt("n8", vcodec="avc1", acodec="none", width=640, height=-360),
    fmt("n9", vcodec="avc1", acodec="none", width=1e3, height=7.9),
]

INFOS = [
    {"formats": YT}, {"formats": SHORT}, {"formats": LOW}, {"formats": NOTES}, {"formats": []}, {},
    {"formats": None}, {"formats": [A_M4A]},
    {"formats": [fmt("x", vcodec="avc1", acodec="none", height=720, fps="nan")]},              # round(nan)
    {"formats": [fmt("x", vcodec="avc1", acodec="none", height="inf")]},                        # int(inf)
    {"formats": [fmt("x", vcodec="avc1", acodec="none", height=720, filesize="nan")]},
    {"formats": [fmt("x", vcodec=5, acodec="none", height=720)]},                               # 5.lower()
    {"formats": [fmt("x", vcodec="avc1", acodec="none", height=720, dynamic_range=1)]},
    {"formats": [fmt("x", vcodec="avc1", acodec="none", height=720, protocol=["m3u8", "x"]),
                 fmt("y", vcodec="avc1", acodec="none", height=720, protocol=7)]},
    {"formats": [fmt("x", vcodec="avc1", acodec="none", height=720, protocol=["https"], tbr=1),
                 fmt("y", vcodec="avc1", acodec="none", height=720, protocol={"m3u8": 1}, tbr=2)]},
    {"formats": ["no es un objeto"]},
    {"formats": {"a": 1}},
]

WANTED = [None, 0, 4320, 1080, 1000, 720, 361, 360, 100, -5]

MB = [0, 1, 1048575, 1048576, 5_000_000, 10_485_759, 10_485_760, 10_485_761, 15_204_352, 1e12, 1.5 * 1048576,
      9.95 * 1048576, 9.949 * 1048576, 0.05 * 1048576, 0.25 * 1048576]
