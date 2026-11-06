"""Genera src/core/src/mcp_help.inc: las tablas de backend/app/mcp_server/help_content.py de
video-yt, en su orden: TOOL_DOMAINS (cada tool del servidor MCP con su dominio) y HELP (la guía
larga de cada dominio, la de `help://<domain>` y `describe_capabilities`).

Ejecutar con el Python de video-yt cuando cambie help_content.py (el test de paridad del MCP,
tests/parity/test_mcp_parity.cpp, lo avisa):

    E:\\proyectos\\video-yt\\.venv\\Scripts\\python.exe tools\\gen\\gen_mcp_help.py [<raíz de video-yt>]
"""
from __future__ import annotations

import sys
from pathlib import Path

OUT = Path(__file__).resolve().parents[2] / "src" / "core" / "src" / "mcp_help.inc"
DEFAULT_VIDEO_YT = Path(r"E:\proyectos\video-yt")
# MSVC no admite un literal de más de 16 KB ni una concatenación de más de 64 KB.
CHUNK = 2000
LIMIT = 60000


def literal(text: str) -> str:
    """El texto como literales de C++ seguidos (UTF-8 tal cual; escapes solo donde hace falta)."""
    parts, cur, size = [], [], 0
    for ch in text:
        if ch == "\\":
            esc = "\\\\"
        elif ch == '"':
            esc = '\\"'
        elif ch == "\n":
            esc = "\\n"
        elif ch == "\t":
            esc = "\\t"
        elif ord(ch) < 0x20 or ch == "\x7f":
            esc = f"\\{ord(ch):03o}"  # octal: como mucho tres cifras, no se come lo que sigue
        elif ch == "?":
            esc = "\\?"  # sin trigrafos
        else:
            esc = ch
        n = len(esc.encode("utf-8"))
        if size + n > CHUNK:
            parts.append("".join(cur))
            cur, size = [], 0
        cur.append(esc)
        size += n
    parts.append("".join(cur))
    total = sum(len(p.encode("utf-8")) for p in parts)
    if total > LIMIT:
        raise SystemExit(f"un texto de {total} bytes no cabe en un literal de MSVC: trocéalo")
    return "\n        ".join(f'"{p}"' for p in parts)


def main() -> None:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_VIDEO_YT
    sys.path.insert(0, str(root / "backend"))
    from app.mcp_server import help_content as hc

    lines = ["// Generado por tools/gen/gen_mcp_help.py desde backend/app/mcp_server/help_content.py de",
             "// video-yt: no editar a mano.", "",
             f"constexpr std::array<ToolDomain, {len(hc.TOOL_DOMAINS)}> kToolDomains{{{{"]
    for tool, domain in hc.TOOL_DOMAINS.items():
        lines.append(f"    {{{literal(tool)}, {literal(domain)}}},")
    lines += ["}};", "", f"constexpr std::array<DomainGuide, {len(hc.HELP)}> kHelp{{{{"]
    for domain, text in hc.HELP.items():
        lines.append(f"    {{{literal(domain)},\n        {literal(text)}}},")
    lines += ["}};", ""]
    OUT.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"{OUT}: {len(hc.TOOL_DOMAINS)} tools, {len(hc.HELP)} guías")


if __name__ == "__main__":
    main()
