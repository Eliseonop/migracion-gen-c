"""Genera src/core/src/py_lower.inc: las tablas de str.lower() de Python.

- Cada carácter que cambia al pasar a minúsculas, con su resultado (puede ser más de un
  carácter: «İ» → «i̇»).
- La regla de la sigma final: «Σ» pasa a «ς» si la precede un carácter con caja y no la
  sigue otro, saltando los que ignoran la caja (handle_capital_sigma de CPython). Python
  no expone esas dos propiedades, así que se deducen preguntándole a str.lower():
    (x + "Σ").lower() acaba en «ς»        ⇔ x tiene caja y no se ignora;
    ("A" + x + "Σ").lower() acaba en «ς»  ⇔ x se ignora, o tiene caja y no se ignora.

Ejecutar con el Python de video-yt cuando cambie su versión de Unicode:

    E:\\proyectos\\video-yt\\.venv\\Scripts\\python.exe tools\\gen\\gen_py_lower.py
"""
from __future__ import annotations

import sys
import unicodedata
from pathlib import Path

OUT = Path(__file__).resolve().parents[2] / "src" / "core" / "src" / "py_lower.inc"
SIGMA = "\u03a3"
FINAL = "\u03c2"


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


def main() -> None:
    lower, cased, ignorable = [], [], []
    for cp in range(sys.maxunicode + 1):
        if 0xD800 <= cp <= 0xDFFF:
            continue
        x = chr(cp)
        if cp != 0x03A3 and x.lower() != x:
            lower.append(f"    {{0x{cp:05X}, {c_string(x.lower())}}},")
        alone = (x + SIGMA).lower().endswith(FINAL)
        after_cased = ("A" + x + SIGMA).lower().endswith(FINAL)
        if alone:
            cased.append(cp)
        elif after_cased:
            ignorable.append(cp)
    fmt = lambda rs: "\n".join(f"    {{0x{a:05X}, 0x{b:05X}}}," for a, b in rs)  # noqa: E731
    cr, ir = ranges(cased), ranges(ignorable)
    text = (f"// Generado por tools/gen/gen_py_lower.py (Unicode {unicodedata.unidata_version}). No editar.\n"
            f"// str.lower() de Python: {{carácter, minúsculas en UTF-8}}, ordenado por carácter (sin «Σ»,\n"
            f"// que depende del contexto).\n"
            f"#ifdef GC_PY_LOWER_MAP\n" + "\n".join(lower) + "\n#endif\n"
            f"// Caracteres con caja que no se ignoran (rangos cerrados, ordenados).\n"
            f"#ifdef GC_PY_LOWER_CASED\n" + fmt(cr) + "\n#endif\n"
            f"// Caracteres que ignoran la caja (rangos cerrados, ordenados).\n"
            f"#ifdef GC_PY_LOWER_IGNORABLE\n" + fmt(ir) + "\n#endif\n")
    OUT.write_text(text, encoding="utf-8", newline="\n")
    print(f"{OUT.name}: {len(lower)} caracteres, {len(cr)} rangos con caja, {len(ir)} que ignoran "
          f"(Unicode {unicodedata.unidata_version})")


if __name__ == "__main__":
    main()
