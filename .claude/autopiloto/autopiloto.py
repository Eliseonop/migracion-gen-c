#!/usr/bin/env python3
"""Autopiloto de la migración: hook `Stop` de Claude Code.

Cada vez que Claude va a terminar el turno, este script decide si debe seguir:

- Sin el interruptor `.claude/autopiloto/activo` no hace nada, así que Claude para.
- Si existe `.claude/autopiloto/bloqueado.md`, deja parar y muestra el motivo (algo necesita a Edu).
- Si dos vueltas seguidas no cambian ningún archivo, deja parar. Así no se gastan créditos en bucle.
- En los demás casos bloquea la parada y le devuelve a Claude la fase en curso, sus tareas
  pendientes (sacadas de docs/TODO.md) y el protocolo. Si en docs/TODO.md hay tareas nuevas
  marcadas como hechas, le pide además lanzar el subagente `revisor-migracion` antes de seguir.

Encender: crear `.claude/autopiloto/activo`. Apagar: borrarlo.
"""
from __future__ import annotations

import hashlib
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
TODO = ROOT / "docs" / "TODO.md"
ACTIVO = HERE / "activo"
BLOQUEADO = HERE / "bloqueado.md"
ESTADO = HERE / "estado.json"
PROTOCOLO = HERE / "protocolo.md"

# Lo que cuenta como avance: si nada de esto cambia entre dos paradas, no hubo trabajo.
VIGILADO = ["src", "tests", "tools", "apps", "cmake", "resources", "docs",
            "CMakeLists.txt", "CMakePresets.json", "vcpkg.json", "README.md", ".gitignore"]
MAX_VUELTAS_SIN_CAMBIOS = 2
MAX_PENDIENTES = 12


def salir(respuesta: dict | None = None) -> None:
    if respuesta:
        # ensure_ascii: la salida no depende de la codificación de la consola de Windows.
        print(json.dumps(respuesta))
    sys.exit(0)


def huella() -> str:
    h = hashlib.sha1()
    for nombre in VIGILADO:
        p = ROOT / nombre
        if p.is_file():
            archivos = [p]
        elif p.is_dir():
            archivos = sorted(f for f in p.rglob("*") if f.is_file() and "__pycache__" not in f.parts)
        else:
            continue
        for f in archivos:
            st = f.stat()
            h.update(f"{f.relative_to(ROOT).as_posix()}|{st.st_size}|{st.st_mtime_ns}\n".encode())
    return h.hexdigest()


def leer_todo() -> tuple[int, str | None, str, list[str], bool]:
    """(casillas hechas, id de la fase en curso, su nombre, pendientes, ¿tiene notas de orden?)"""
    texto = TODO.read_text(encoding="utf-8")
    hechas = len(re.findall(r"^\s*- \[x\]", texto, re.M))

    fase, nombre = None, ""
    for marca in ("🟡", "⬜"):  # en curso; si no hay ninguna, la primera sin empezar
        m = re.search(rf"^\|\s*\*\*(F\d+)\*\*\s*([^|]*)\|[^|]*\|\s*{marca}", texto, re.M)
        if m:
            fase, nombre = m.group(1), m.group(2).strip()
            break
    if fase is None:
        return hechas, None, "", [], False

    m = re.search(rf"^## {fase}\b.*?$(.*?)(?=^## |\Z)", texto, re.M | re.S)
    seccion = m.group(1) if m else ""
    pendientes, sub = [], ""
    for linea in seccion.splitlines():
        if linea.startswith("### "):
            sub = re.split(r"[:(]", linea[4:])[0].strip()  # «Comandos: 40 hechos (…)» → «Comandos»
        elif re.match(r"^\s*- \[ \]", linea):
            item = re.sub(r"^\s*- \[ \]\s*", "", linea).strip()
            if len(item) > 170:
                item = item[:167] + "..."
            pendientes.append(f"- ({sub}) {item}" if sub else f"- {item}")
    return hechas, fase, nombre, pendientes, "### Notas" in seccion


def main() -> None:
    try:
        json.loads(sys.stdin.buffer.read().decode("utf-8") or "{}")
    except (ValueError, UnicodeDecodeError):
        pass  # la entrada no hace falta; solo se consume

    if not ACTIVO.exists():
        salir()

    if BLOQUEADO.exists():
        motivo = BLOQUEADO.read_text(encoding="utf-8").strip().splitlines()
        primera = motivo[0].lstrip("# ").strip() if motivo else "sin motivo"
        salir({"systemMessage": f"Autopiloto en pausa: {primera}. "
                                f"Detalle en .claude/autopiloto/bloqueado.md (bórralo para reanudar)."})

    try:
        estado = json.loads(ESTADO.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        estado = {}

    h = huella()
    sin_cambios = estado.get("sin_cambios", 0) + 1 if estado.get("huella") == h else 0
    vuelta = estado.get("vuelta", 0) + 1
    hechas, fase, nombre, pendientes, con_notas = leer_todo()

    nuevas = hechas - estado["hechas"] if isinstance(estado.get("hechas"), int) else 0
    cambio_fase = bool(estado.get("fase")) and fase != estado.get("fase")

    ESTADO.write_text(json.dumps({"huella": h, "sin_cambios": sin_cambios, "vuelta": vuelta,
                                  "hechas": hechas, "fase": fase}, indent=2), encoding="utf-8")

    if sin_cambios >= MAX_VUELTAS_SIN_CAMBIOS:
        salir({"systemMessage": f"Autopiloto detenido: {sin_cambios} vueltas seguidas sin cambiar "
                                f"ningún archivo. Se reanuda solo en cuanto haya avance."})

    partes = [f"Autopiloto de la migración, vuelta {vuelta}. No termines todavía: sigue trabajando."]
    if fase:
        partes.append(f"\nFase en curso: {fase}, {nombre} (docs/TODO.md).")
        if pendientes:
            partes.append("Pendiente en esa fase:")
            partes.extend(pendientes[:MAX_PENDIENTES])
            if len(pendientes) > MAX_PENDIENTES:
                partes.append(f"- … y {len(pendientes) - MAX_PENDIENTES} más")
            if con_notas:
                partes.append("La sección tiene notas con el orden y los detalles: síguelas.")
        else:
            partes.append(f"La fase {fase} ya no tiene casillas pendientes: ciérrala (tabla «Estado de "
                          "un vistazo» y §14 del plan) y pasa a la siguiente.")
    else:
        partes.append("\nNo encuentro ninguna fase en curso ni sin empezar en docs/TODO.md: "
                      "comprueba la tabla «Estado de un vistazo».")

    if nuevas > 0 or cambio_fase:
        motivo = f"se marcaron {nuevas} tareas como hechas" if nuevas > 0 else ""
        if cambio_fase:
            motivo += (" y " if motivo else "") + f"la fase pasó de {estado.get('fase')} a {fase}"
        partes.append(f"\n>>> REVISIÓN: desde la última vuelta {motivo}. Antes de seguir, lanza el "
                      "subagente `revisor-migracion`" + (" con --limpio (cierre de fase)" if cambio_fase else "")
                      + ", corrige lo que marque como bloqueante y continúa con la tarea que proponga.")

    if sin_cambios == 1:
        partes.append("\nOjo: la última vuelta no cambió ningún archivo. Si lo siguiente necesita a Edu, "
                      "escribe el motivo en .claude/autopiloto/bloqueado.md y termina. Si no, avanza.")

    if PROTOCOLO.exists():
        partes.append("\n" + PROTOCOLO.read_text(encoding="utf-8").strip())

    salir({"decision": "block", "reason": "\n".join(partes)})


if __name__ == "__main__":
    main()
