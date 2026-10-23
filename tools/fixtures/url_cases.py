"""Casos de URLs (urllib.parse de Python 3.14 y los enlaces de YouTube de youtube_audio.py)."""
from __future__ import annotations

NBSP = chr(0xA0)

URLS = [
    "", "   ", "https://www.youtube.com/watch?v=dQw4w9WgXcQ",
    "youtube.com/watch?v=abc", "www.youtube.com/watch?v=abc&t=10", "m.youtube.com/watch?v=xyz",
    "https://music.youtube.com/watch?v=mus1&list=PL1", "https://youtu.be/short1", "youtu.be/short2?t=5",
    "https://youtu.be/", "https://youtu.be//a/b/", "https://YOUTU.BE/CaSe", "https://WWW.YouTube.COM/watch?V=no&v=yes",
    "https://www.youtube.com/shorts/sh0rt", "https://www.youtube.com/live/l1ve?feature=share",
    "https://www.youtube.com/embed/emb3d", "https://www.youtube.com/embed/", "https://www.youtube.com/watch",
    "https://www.youtube.com/watch?v=", "https://www.youtube.com/watch?v=&v=second", "https://www.youtube.com/watch?v",
    "https://www.youtube.com/watch?%76=encoded", "https://www.youtube.com/watch?v=a%20b+c", "https://www.youtube.com/watch?v=%E2%82",
    "https://www.youtube.com/watch?v=%ff%41", "https://www.youtube.com/watch?v=%F0%9F%98%80", "https://www.youtube.com/watch?v=x;y",
    "https://www.youtube.com/watch;params?v=p1", "https://www.youtube.com/a;b/c;d?v=semi", "https://user:pw@www.youtube.com:8080/watch?v=auth",
    "https://www.youtube.com.evil.com/watch?v=evil", "https://notyoutube.com/watch?v=n", "https://evilyoutu.be/x",
    "https://youtube.co/watch?v=co", "https://foo.youtube.com/watch?v=sub", "https://.youtube.com/x",
    "ftp://youtube.com/watch?v=ftp", "HTTPS://YOUTUBE.COM/watch?v=upper", "https:/youtube.com/watch?v=oneslash",
    "//youtube.com/watch?v=noscheme", "youtube.com:443/watch?v=port", "youtube.com/watch?v=a#v=frag",
    "https://youtube.com/watch#?v=frag2", "https://youtube.com?v=nopath", "https://youtube.com/shorts",
    "https://youtube.com//shorts//sid//", "https://youtube.com/SHORTS/up", "  \t https://youtu.be/ws  \n",
    NBSP + "https://youtu.be/nbsp" + NBSP, "https://you\ntu.be/nl", "https://youtu.be/t\tab", "\x01https://youtu.be/c0",
    "https://[::1]/watch?v=v6", "https://[::1]:80/watch?v=v6p", "https://[fe80::1%25eth0]/x", "https://[FE80::A%Zone]/x",
    "https://[1.2.3.4]/x", "https://[v1.fe]/x", "https://[v1.]/x", "https://[vz.a]/x", "https://[::1/x",
    "https://::1]/x", "https://a[::1]/x", "https://[::1]x/x", "https://[::1]:x/x", "https://[]/x",
    "https://[1:2:3:4:5:6:7:8]/x", "https://[1:2:3:4:5:6:7:8:9]/x", "https://[1::2::3]/x", "https://[:1:2:3:4:5:6:7]/x",
    "https://[1:2:3:4:5:6:7:]/x", "https://[::ffff:1.2.3.4]/x", "https://[::ffff:1.2.3.04]/x", "https://[::12345]/x",
    "https://[1:2:3:4:5:6:7::]/x", "https://[::1:2:3:4:5:6:7]/x", "https://[::1:2:3:4:5:6:7:8]/x", "https://[g::1]/x",
    "https://a[b]@youtube.com/x", "https://[::1]@youtube.com/watch?v=at", "https://youtube.com@evil.com/x",
    "https://evil.com@youtube.com/watch?v=at2", "https://youtube.com\u2100evil/x", "https://you\uff0ftube.com/x",
    "https://y\u00f6utube.com/x", "https://www.youtube.com\uff1a80/x", "https://\u0130stanbul.youtube.com/x",
    "https://youtube.com/watch?v=\u00f1and\u00fa", "https://youtube.com/watch?v=%C3\u00f1%B1", "https://youtube.com/watch?a=1&&v=amp&v=2",
    "https://youtube.com/watch?v=one;v=two", "https://youtube.com/watch?=x&v=empty-name", "https://youtube.com/watch?v==eq",
    "1http://youtube.com/x", "h.t-t+p://youtube.com/x", "ht_tp://youtube.com/watch?v=bad", "a b://youtube.com/x",
    "youtube.com/watch?v=1://", "mailto:youtube.com", "javascript://youtube.com/%0Aalert(1)", "https://youtube.com:abc/x",
    "https://[::1%.YOUTUBE.COM]/x", "https://[::1%YOUTU.BE]/abc", "https://x%FF.YOUTUBE.COM/",
    "https://[fe80::1%25Eth0.YouTube.com]/watch?v=zona",
    "https://you;tube.com/x", "https://[v.a]/x", "https://[v1a]/x", "https://www.youtu.be/www", "https://m.youtu.be/m",
]

UNQUOTE = [
    "", "abc", "%", "%4", "%41", "%4G", "a%zzb", "%%41", "%e2%82%ac", "%e2%82", "%ff%41", "%ED%A0%80",
    "%F0%9F%98%80", "%F4%90%80%80", "%C0%AF", "%E0%80%80", "%F0%80%80%80", "%C2", "%C2%41", "%F0%9F%98",
    "\u00f1%41\u00f1%C3%B1", "%C3\u00f1%B1", "+%2B+", "%2b%2B", "%00x", "%7f%80%bf",
]

DOWNLOADS = [
    "https://www.pexels.com/video/1.mp4", "https://videos.pexels.com/x.mp4", "http://pexels.com/a", "https://PEXELS.COM/a",
    "https://media.giphy.com/m.gif", "https://player.vimeo.com/v", "https://vod-progressive.vimeocdn.com/x",
    "https://cdn.pixabay.com/x", "https://images.unsplash.com/x", "https://unsplash.com.evil.com/x",
    "https://evilpexels.com/x", "https://wpexels.com/x", "https://w.w.pexels.com/x", "https://web.pexels.com/x",
    "https://wwwgiphy.com/x", "https://www.www.giphy.com/x", "ftp://pexels.com/x", "pexels.com/x", "//pexels.com/x",
    "https:///x", "https://user@giphy.com:8443/x", "https://[::1]/x", "https://[::1/x", "  https://giphy.com/x  ",
    "https://pexels.com./x", "https://.pexels.com/x", "HTTPS://pixabay.com/x", "https://xn--pexels.com/x",
]

QUERIES = [
    "", "&", "a", "a=", "a=1", "a=1&b=2&a=3", "=x", "a=b=c", "a+b=c+d", "a%3Db=c", "a=1;b=2", "&&a=1&&",
    "v=%E2%82", "%76=x&v=y",
]
