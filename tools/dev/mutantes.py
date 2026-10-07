"""Pruebas de mutación: cada mutante de un grupo debe hacer fallar los tests de su etiqueta.

    python tools/dev/mutantes.py <grupo> [preset] [filtro]

`grupo`: eval (evaluación por clip, tests [eval]) o layers (capas de un instante, tests
[layers]). `preset`: la build ya compilada (por defecto mingw-release). `filtro`: solo los
mutantes cuyo nombre contiene ese texto.

Aplica cada cambio a la fuente, recompila gc_tests, pasa los tests [eval] y deshace el
cambio (también si algo falla). Un mutante «NO DETECTADO» es un hueco de las fixtures,
salvo que sea equivalente (da el mismo resultado): esos van anotados en la lista.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CLION = Path(os.environ.get("CLION_HOME", r"C:\Program Files\JetBrains\CLion 2026.2.3"))
ENV = dict(os.environ)
ENV["PATH"] = os.pathsep.join([str(CLION / "bin" / "mingw" / "bin"), str(CLION / "bin" / "ninja" / "win" / "x64"),
                               str(CLION / "bin" / "cmake" / "win" / "x64" / "bin"), ENV["PATH"]])

# (nombre, archivo, texto original, texto mutado)
EVAL = [
    ("clip_fx: salida mide desde el inicio", "src/core/src/clip_fx.cpp",
     "clamp01((duration - local_t) / ed)", "clamp01(local_t / ed)"),
    ("clip_fx: disolver no funde", "src/core/src/clip_fx.cpp",
     '|| kind == "dissolve"', ""),
    ("clip_fx: cortinilla con max", "src/core/src/clip_fx.cpp",
     "fx.wipe = std::min(fx.wipe, ep);", "fx.wipe = std::max(fx.wipe, ep);"),
    ("clip_fx: texto vacío no es none", "src/core/src/clip_fx.cpp",
     "return value.empty() ? kNone : value;", "return value;"),
    ("clip_fx: zoom de salida al revés", "src/core/src/clip_fx.cpp",
     "fx.scale *= lerp(1.0, kFxZoomFrom, 1.0 - ep);", "fx.scale *= lerp(1.0, kFxZoomFrom, ep);"),
    ("clip_fx: clamp01 con NaN da 1", "src/core/src/clip_fx.cpp",
     "double clamp01(double v) { return std::min(1.0, std::max(0.0, v)); }",
     "double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }"),
    ("fx_anims: muestreo a 5 decimales", "src/core/src/fx_anims.cpp",
     "static_cast<double>(n), 6));", "static_cast<double>(n), 5));"),
    ("fx_anims: sin reparto de entrada y salida", "src/core/src/fx_anims.cpp",
     "if (d_total > 0 && total > d_total) {", "if (false) {"),
    ("fx_anims: bucle con fmod", "src/core/src/fx_anims.cpp",
     "u = py_mod(t / a.dur, 1.0);", "u = std::fmod(t / a.dur, 1.0);"),
    ("fx_anims: combo sin la duración del clip", "src/core/src/fx_anims.cpp",
     "if (slot == \"combo\") {\n            dur = d_total;", "if (false) {\n            dur = d_total;"),
    ("fx_anims: mínimo de 0,01 s", "src/core/src/fx_anims.cpp",
     "std::max(0.05, dur)", "std::max(0.01, dur)"),
    ("fx_anims: escala que suma", "src/core/src/fx_anims.cpp",
     "pose.scale * d.scale", "pose.scale + d.scale - 1"),
    ("fx_anims: duración no finita vale", "src/core/src/fx_anims.cpp",
     "if (!std::isfinite(dur) || dur <= 0) {", "if (dur <= 0) {"),
    ("fx_anims: dur 0 vale", "src/core/src/fx_anims.cpp", "|| dur <= 0) {", "|| dur < 0) {"),
    ("fx_anims: salida sin max(0, D - dur)", "src/core/src/fx_anims.cpp",
     "add(std::max(0.0, d_total - a.dur), d_total);", "add(d_total - a.dur, d_total);"),
    ("fx_anims: unit con NaN da 1", "src/core/src/fx_anims.cpp",
     "double unit(double v) { return std::min(1.0, std::max(0.0, v)); }",
     "double unit(double v) { return std::max(0.0, std::min(1.0, v)); }"),
    ("fx_anims: outBack sin el término cuadrático", "src/core/src/fx_anims.cpp",
     "+ kC1 * std::pow(x - 1, 2.0)", ""),
    ("fx_anims: inOutSine con signo cambiado", "src/core/src/fx_anims.cpp",
     "return -(std::cos(std::numbers::pi * x) - 1) / 2;", "return (std::cos(std::numbers::pi * x) - 1) / 2;"),
    ("fx_anims: kC4 un poco mayor (lo tapa una tolerancia ancha)", "src/core/src/fx_anims.cpp",
     "const double kC4 = (2 * std::numbers::pi) / 3;", "const double kC4 = (2 * std::numbers::pi) / 3 * (1 + 1e-14);"),
    ("fx_anims: ranura distinta aceptada", "src/core/src/fx_anims.cpp",
     "if (anim == nullptr || field(*anim, \"slot\") != slot) {", "if (anim == nullptr) {"),
    ("clip_adjust: fantasma sin sufijo", "src/core/src/clip_adjust.cpp",
     "ghost.id += kGhostSuffix;", ""),
    ("clip_adjust: máscaras del fantasma sin target", "src/core/src/clip_adjust.cpp",
     'm["target"] = "clip";', ""),
    ("clip_adjust: fantasma con volumen", "src/core/src/clip_adjust.cpp",
     "ghost.volume = 0.0;", ""),
    ("clip_adjust: NaN cuenta", "src/core/src/clip_adjust.cpp",
     "return n && std::isfinite(*n) ? *n : 0.0;", "return n ? *n : 0.0;"),
    # Equivalente: con blancos a 0, k / (1 − 0) y k · (1 + 0) son el mismo valor.
    ("clip_adjust: blancos con >= (EQUIVALENTE)", "src/core/src/clip_adjust.cpp",
     "return whites > 0 ?", "return whites >= 0 ?"),
    ("clip_adjust: el tono no se quita", "src/core/src/clip_adjust.cpp",
     "if (std::ranges::find(kColorKeys, key) == kColorKeys.end()) {",
     "if (key == \"hue\" || std::ranges::find(kColorKeys, key) == kColorKeys.end()) {"),
    ("clip_adjust: texto con dos pasadas", "src/core/src/clip_adjust.cpp",
     'kind != "shape")', 'kind != "shape" && kind != "text")'),
    ("clip_adjust: capa de ajuste con dos pasadas", "src/core/src/clip_adjust.cpp",
     'kind != "shape")', 'kind != "shape" && kind != "adjustment")'),
    ("clip_adjust: temperatura al revés", "src/core/src/clip_adjust.cpp",
     "k * (1.0 + 0.18 * te), k, k * (1.0 - 0.18 * te)", "k * (1.0 - 0.18 * te), k, k * (1.0 + 0.18 * te)"),
    ("clip_adjust: 0.213 desplazado (lo tapa una tolerancia ancha)", "src/core/src/clip_adjust.cpp",
     "{0.213 + 0.787 * a - 0.213 * b,", "{0.213000000000001 + 0.787 * a - 0.213 * b,"),
    ("clip_adjust: 0.18 desplazado (lo tapa una tolerancia ancha)", "src/core/src/clip_adjust.cpp",
     "{k * (1.0 + 0.18 * te), k,", "{k * (1.0 + 0.18000000000001 * te), k,"),
    ("blend: modo sin validar", "src/core/include/gc/core/blend.hpp",
     'return it == kBlendModes.end() ? "normal" : *it;', "return mode;"),
    ("blend: typo en darken", "src/core/include/gc/core/blend.hpp", '"darken",', '"darkn",'),
    ("blend: typo en exclusion", "src/core/include/gc/core/blend.hpp", '"exclusion",', '"exclusio",'),
    ("blend: typo en color_dodge", "src/core/include/gc/core/blend.hpp", '"color_dodge",', '"color-dodge",'),
]

LAYERS = [
    ("paints: sin motion", "src/core/src/layers.cpp", 'k == "motion" || ', ""),
    ("paints: con audio", "src/core/src/layers.cpp", 'k == "text";', 'k == "text" || k == "audio";'),
    ("desactivados visibles", "src/core/src/layers.cpp", "if (!paints(*c) || c->disabled) {", "if (!paints(*c)) {"),
    ("pistas ocultas visibles", "src/core/src/layers.cpp",
     "if (tr == track_by_id.end() || tr->second->hidden) {", "if (tr == track_by_id.end()) {"),
    ("margen con >", "src/core/src/layers.cpp",
     "head >= start - rules.seek_margin", "head > start - rules.seek_margin"),
    ("fin cerrado", "src/core/src/layers.cpp",
     "head < start + clip_timeline_duration(*c)", "head <= start + clip_timeline_duration(*c)"),
    ("fin con la duración de la fuente", "src/core/src/layers.cpp",
     "head < start + clip_timeline_duration(*c)", "head < start + clip_source_duration(*c)"),
    ("pista sin capa al frente", "src/core/src/layers.cpp", "? -1L :", "? 1000L :"),
    ("índice del primer id repetido", "src/core/src/layers.cpp",
     "index[timeline.clips[i]->id] = i;", "index.try_emplace(timeline.clips[i]->id, i);"),
    ("pista del primer id repetido", "src/core/src/layers.cpp",
     "track_by_id[t.id] = &t;", "track_by_id.try_emplace(t.id, &t);"),
    ("pila: capa del primer id repetido", "src/core/src/timeline_rules.cpp",
     "out[t.id] = layer++;", "out.try_emplace(t.id, layer++);"),
    ("enterradas: nunca hay vídeo encima", "src/core/src/timeline_rules.cpp",
     "video_above = true;", "video_above = false;"),
    ("render: el que empieza antes delante", "src/core/src/layers.cpp",
     "return a.start < b.start;", "return a.start > b.start;"),
    ("render: sin acotar el inicio a 0", "src/core/src/layers.cpp",
     "rules.by_start ? std::max(0.0, c->start) : c->start", "c->start"),
    ("preview: desempate por inicio", "src/core/src/layers.cpp",
     "if (rules.by_start && a.start != b.start) {", "if (a.start != b.start) {"),
    ("videos_at con textos", "src/core/src/layers.cpp", 'return c->kind == "text"; });', "return false; });"),
]

GROUPS = {"eval": (EVAL, "[eval]"), "layers": (LAYERS, "[layers]")}


def run(cmd: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, cwd=REPO, env=ENV, capture_output=True, text=True, encoding="utf-8",
                          errors="replace")


def main() -> int:
    if len(sys.argv) < 2 or sys.argv[1] not in GROUPS:
        print(f"Uso: mutantes.py <{'|'.join(GROUPS)}> [preset] [filtro]")
        return 2
    group, tag = GROUPS[sys.argv[1]]
    build = REPO / "build" / (sys.argv[2] if len(sys.argv) > 2 else "mingw-release")
    only = sys.argv[3] if len(sys.argv) > 3 else ""
    mutants = [m for m in group if only in m[0]]
    cmake = shutil.which("cmake", path=ENV["PATH"])
    tests = build / "tests" / "gc_tests.exe"
    if cmake is None or not tests.exists():
        print("Falta cmake o la build: compila antes con tools/dev/verificar.sh")
        return 2
    caught = 0
    for name, rel, old, new in mutants:
        path = REPO / rel
        original = path.read_text(encoding="utf-8")
        if old not in original:
            print(f"??            {name}: no encuentro el texto")
            continue
        path.write_text(original.replace(old, new, 1), encoding="utf-8", newline="\n")
        try:
            b = run([cmake, "--build", str(build), "--target", "gc_tests"])
            if b.returncode != 0:
                print(f"NO COMPILA    {name}\n{b.stdout[-800:]}")
                continue
            ok = run([str(tests), tag]).returncode != 0
            caught += ok
            print(("detectado     " if ok else "NO DETECTADO  ") + name)
        finally:
            path.write_text(original, encoding="utf-8", newline="\n")
    run([cmake, "--build", str(build), "--target", "gc_tests"])
    print(f"{caught}/{len(mutants)} detectados")
    return 0


if __name__ == "__main__":
    sys.exit(main())
