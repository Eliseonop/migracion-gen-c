"""Genera src/core/src/mcp_server_data.inc: lo que el servidor MCP de video-yt dice de sí mismo, tal
cual: el JSON de tools/list (nombre, descripción, esquema de los argumentos, anotaciones y `_meta` de
cada tool), el de resources/list y resources/templates/list, las instrucciones y lo que se pasa por
initialize. El servidor C++ solo lista las tools que ya tiene.

Ejecutar con el Python de video-yt cuando cambie alguna tool (el test de paridad del MCP,
tests/parity/test_mcp_server_parity.cpp, lo avisa):

    E:\\proyectos\\video-yt\\.venv\\Scripts\\python.exe tools\\gen\\gen_mcp_tools.py [<raíz de video-yt>]
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_mcp_help import DEFAULT_VIDEO_YT, literal  # noqa: E402

OUT = Path(__file__).resolve().parents[2] / "src" / "core" / "src" / "mcp_server_data.inc"
# MSVC no admite un literal de más de 16 KB ni una concatenación de más de 64 KB.
PIECE = 40000


def pieces(name: str, value) -> list[str]:
    """Un texto largo como una tabla de literales, que se juntan al usarla."""
    text = value if isinstance(value, str) else json.dumps(value, ensure_ascii=False, separators=(",", ":"))
    chunks: list[str] = []
    cur: list[str] = []
    size = 0
    for ch in text:
        n = len(ch.encode("utf-8"))
        if size + n > PIECE:
            chunks.append("".join(cur))
            cur, size = [], 0
        cur.append(ch)
        size += n
    chunks.append("".join(cur))
    body = ",\n    ".join(literal(c) for c in chunks)
    return [f"constexpr std::array<std::string_view, {len(chunks)}> {name}{{{{", f"    {body},", "}};", ""]


def wire(item) -> dict:
    """Como va por el cable: las claves de primer nivel y las anotaciones, en orden alfabético."""
    d = dict(sorted(item.model_dump(by_alias=True, exclude_none=True, mode="json").items()))
    if "annotations" in d:
        d["annotations"] = dict(sorted(d["annotations"].items()))
    return d


def dump(items) -> list:
    return [wire(x) for x in items]


def main() -> None:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_VIDEO_YT
    sys.path.insert(0, str(root / "backend"))
    import anyio
    from mcp_types import version

    from app.mcp_server import server

    tools = dump(anyio.run(server.mcp.list_tools))
    resources = dump(anyio.run(server.mcp.list_resources))
    templates = dump(anyio.run(server.mcp.list_resource_templates))

    lines = ["// Generado por tools/gen/gen_mcp_tools.py desde backend/app/mcp_server de video-yt: no editar a mano.", ""]
    lines += pieces("kToolsJson", tools)
    lines += pieces("kResourcesJson", resources)
    lines += pieces("kTemplatesJson", templates)
    lines += pieces("kInstructions", server.INSTRUCTIONS)
    versions = version.HANDSHAKE_PROTOCOL_VERSIONS
    lines.append(f"constexpr std::array<std::string_view, {len(versions)}> kProtocolVersions{{{{")
    lines += [f'    "{v}",' for v in versions]
    lines += ["}};", ""]
    OUT.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"{OUT}: {len(tools)} tools, {len(resources)} resources, {len(templates)} plantillas")


if __name__ == "__main__":
    main()
