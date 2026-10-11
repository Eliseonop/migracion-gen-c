"""Genera src/core/src/py_text.inc: más tablas de texto de Python, para los personajes stick.

- El PLEGADO de cada carácter no ASCII: lo que queda de él en
  `"".join(ch for ch in unicodedata.normalize("NFKD", x) if not unicodedata.combining(ch)).lower()`,
  con cada carácter que no es [a-z0-9] cambiado por «_» (`_norm` y `_slug` de stick_library).
  Se guardan los que dan «» (marcas que desaparecen) o llevan alguna letra o cifra; el resto
  vale «_». Basta con cada carácter por separado: NFKD solo reordena las marcas con clase de
  combinación, que se quitan todas, y la sigma final da «σ» o «ς», que son «_» igual.
- Los dígitos decimales (`str.isdecimal()`, el `\\d` de las regex de texto de Python).
- `str.title()`: lo que da cada carácter que cambia cuando va a principio de palabra
  (`x.title()`, puede ser más de un carácter: «ß» → «Ss») y los caracteres con caja
  (`_PyUnicode_IsCased`), que se deducen preguntando a title(): `(x + "a").title()` acaba en
  «a» ⇔ x tiene caja.
- `str.upper()`: lo que da cada carácter que cambia (`x.upper()`, sin contexto; puede ser más
  de un carácter: «ß» → «SS»), para May./min. del texto (`apply_text_case`).

Ejecutar con el Python de video-yt cuando cambie su versión de Unicode:

    E:\\proyectos\\video-yt\\.venv\\Scripts\\python.exe tools\\gen\\gen_py_text.py
"""
from __future__ import annotations

import re
import sys
import unicodedata
from pathlib import Path

OUT = Path(__file__).resolve().parents[2] / "src" / "core" / "src" / "py_text.inc"


def c_string(s: str) -> str:
    """Literal de C++ con el UTF-8 escapado byte a byte (no depende de la codificación del fuente)."""
    return '"' + "".join(f"\\x{b:02X}" for b in s.encode("utf-8")) + '"'


def ranges(cps: list[int]) -> list[tuple[int, int]]:
    out: list[tuple[int, int]] = []
    for cp in cps:
        if out and out[-1][1] == cp - 1:
            out[-1] = (out[-1][0], cp)
        else:
            out.append((cp, cp))
    return out


def fold(x: str) -> str:
    s = unicodedata.normalize("NFKD", x)
    s = "".join(ch for ch in s if not unicodedata.combining(ch)).lower()
    return "".join(c if re.fullmatch(r"[a-z0-9]", c) else "_" for c in s)


def main() -> None:
    folds, decimal, title, cased, upper = [], [], [], [], []
    for cp in range(sys.maxunicode + 1):
        if 0xD800 <= cp <= 0xDFFF:
            continue
        x = chr(cp)
        if cp >= 0x80:
            f = fold(x)
            if f == "" or re.search(r"[a-z0-9]", f):
                folds.append(f"    {{0x{cp:05X}, {c_string(f)}}},")
        if x.isdecimal():
            decimal.append(cp)
        if x.title() != x:
            title.append(f"    {{0x{cp:05X}, {c_string(x.title())}}},")
        if (x + "a").title().endswith("a"):
            cased.append(cp)
        if x.upper() != x:
            upper.append(f"    {{0x{cp:05X}, {c_string(x.upper())}}},")
    fmt = lambda rs: "\n".join(f"    {{0x{a:05X}, 0x{b:05X}}}," for a, b in rs)  # noqa: E731
    dr, cr = ranges(decimal), ranges(cased)
    text = (f"// Generado por tools/gen/gen_py_text.py (Unicode {unicodedata.unidata_version}). No editar.\n"
            f"// Plegado NFKD de stick_library: {{carácter no ASCII, resultado}} de los que no dan «_».\n"
            f"#ifdef GC_PY_FOLD_MAP\n" + "\n".join(folds) + "\n#endif\n"
            f"// Dígitos decimales: str.isdecimal() (rangos cerrados, ordenados).\n"
            f"#ifdef GC_PY_DECIMAL\n" + fmt(dr) + "\n#endif\n"
            f"// str.title() a principio de palabra: {{carácter, resultado en UTF-8}}, ordenado.\n"
            f"#ifdef GC_PY_TITLE_MAP\n" + "\n".join(title) + "\n#endif\n"
            f"// Caracteres con caja para str.title() (rangos cerrados, ordenados).\n"
            f"#ifdef GC_PY_TITLE_CASED\n" + fmt(cr) + "\n#endif\n"
            f"// str.upper(): {{carácter, resultado en UTF-8}} de los que cambian, ordenado.\n"
            f"#ifdef GC_PY_UPPER_MAP\n" + "\n".join(upper) + "\n#endif\n")
    OUT.write_text(text, encoding="utf-8", newline="\n")
    print(f"{OUT.name}: {len(folds)} plegados, {len(dr)} rangos de dígitos, {len(title)} títulos, "
          f"{len(cr)} rangos con caja, {len(upper)} mayúsculas (Unicode {unicodedata.unidata_version})")


if __name__ == "__main__":
    main()
