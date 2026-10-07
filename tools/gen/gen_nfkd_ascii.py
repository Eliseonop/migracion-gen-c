"""Genera src/core/src/nfkd_ascii.inc: lo que queda en ASCII de cada carácter no ASCII tras
unicodedata.normalize("NFKD", c).encode("ascii", "ignore"), como hace sound_design.norm.

Solo se guardan los caracteres que dejan algo (unos 3000). Ejecutar con el Python de
video-yt cuando cambie su versión de Unicode:

    E:\\proyectos\\video-yt\\.venv\\Scripts\\python.exe tools\\gen\\gen_nfkd_ascii.py
"""
from __future__ import annotations

import sys
import unicodedata
from pathlib import Path

OUT = Path(__file__).resolve().parents[2] / "src" / "core" / "src" / "nfkd_ascii.inc"


def c_string(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main() -> None:
    rows = []
    for cp in range(0x80, sys.maxunicode + 1):
        if 0xD800 <= cp <= 0xDFFF:
            continue
        out = unicodedata.normalize("NFKD", chr(cp)).encode("ascii", "ignore").decode("ascii")
        if out:
            rows.append(f"    {{0x{cp:05X}, {c_string(out)}}},")
    text = (f"// Generado por tools/gen/gen_nfkd_ascii.py (Unicode {unicodedata.unidata_version}). No editar.\n"
            f"// {{carácter, lo que queda en ASCII tras NFKD}}, ordenado por carácter.\n" + "\n".join(rows) + "\n")
    OUT.write_text(text, encoding="utf-8", newline="\n")
    print(f"{OUT.name}: {len(rows)} caracteres (Unicode {unicodedata.unidata_version})")


if __name__ == "__main__":
    main()
