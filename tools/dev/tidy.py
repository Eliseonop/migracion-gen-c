"""clang-tidy (con el .clang-tidy del repo) sobre todo el código: src, apps y tests.

    python tools/dev/tidy.py [--fix] [filtro]

`filtro`: solo los archivos cuya ruta contiene ese texto. `--fix`: aplica los arreglos que
clang-tidy sabe hacer (revisa el diff y compila después). Sale con error si hay algún aviso.

Usa el clang-tidy y la MinGW de CLion con una build propia, build/tidy (mingw-debug sin el
escaneo de módulos de C++20, cuyas opciones de GCC clang no entiende). Ese clang-tidy no
trae las cabeceras internas de clang: las de GCC sirven (-idirafter), salvo las de
intrínsecos x86, que usan builtins de GCC; windows.h las incluye y se sustituyen por las
vacías de tools/dev/tidy_shim (nuestro código no usa intrínsecos).

Lo que MinGW no compila (gc_media, gc_render, gc_export y sus tests, que necesitan FFmpeg y
Skia de vcpkg) se analiza con la base de compilación de la build de MSVC (build/msvc-vcpkg-debug,
la de tools/dev/verificar_msvc.bat) y el entorno de vcvars64.bat (VCVARS), cuyo INCLUDE da las
cabeceras de MSVC. Si esa build no existe, se avisa y esos archivos no se analizan.
"""
from __future__ import annotations

import collections
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
def _clion() -> Path:
    """CLION_HOME o, si no está, el CLion más nuevo de Program Files\\JetBrains."""
    if os.environ.get("CLION_HOME"):
        return Path(os.environ["CLION_HOME"])
    found = sorted(Path(r"C:\Program Files\JetBrains").glob("CLion *"))
    return found[-1] if found else Path(r"C:\Program Files\JetBrains\CLion")


CLION = _clion()
CLANG_TIDY = CLION / "bin" / "clang" / "win" / "x64" / "bin" / "clang-tidy.exe"
MINGW = CLION / "bin" / "mingw"
ENV = dict(os.environ)
ENV["PATH"] = os.pathsep.join([str(MINGW / "bin"), str(CLION / "bin" / "ninja" / "win" / "x64"),
                               str(CLION / "bin" / "cmake" / "win" / "x64" / "bin"), ENV["PATH"]])
BUILD = REPO / "build" / "tidy"
MSVC_BUILD = REPO / "build" / "msvc-vcpkg-debug"
VCVARS = Path(os.environ.get("VCVARS", r"D:\visual\VC\Auxiliary\Build\vcvars64.bat"))
DIAG = re.compile(r"^(?P<file>.*?):(?P<line>\d+):(?P<col>\d+): (?:warning|error): (?P<msg>.*) \[(?P<check>[\w\-.,]+)\]$",
                  re.M)


def gcc_include() -> Path:
    """Las cabeceras internas de GCC (stddef.h, mm_malloc.h…) de la MinGW de CLion."""
    found = sorted((MINGW / "lib" / "gcc" / "x86_64-w64-mingw32").glob("*/include"))
    if not found:
        sys.exit(f"No encuentro las cabeceras de GCC en {MINGW}")
    return found[-1]


def configure() -> None:
    # Siempre: con una build vieja, compile_commands.json no tendría los módulos o las
    # dependencias nuevas (y clang-tidy no encontraría sus cabeceras). Sin cambios, tarda poco.
    cmake = shutil.which("cmake", path=ENV["PATH"])
    r = subprocess.run([cmake, "--preset", "mingw-debug", "-B", str(BUILD), "-DCMAKE_CXX_SCAN_FOR_MODULES=OFF"],
                       cwd=REPO, env=ENV, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"Falla la configuración de {BUILD}:\n{r.stdout[-2000:]}{r.stderr[-2000:]}")


def sources(build: Path) -> set[Path]:
    """Los archivos nuestros (src, apps y tests) de la base de compilación de `build`."""
    db = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    out = set()
    for entry in db:
        f = Path(entry["directory"], entry["file"]).resolve()
        rel = f.relative_to(REPO) if f.is_relative_to(REPO) else None
        if rel is not None and rel.parts[0] in ("src", "apps", "tests"):
            out.add(f)
    return out


def msvc_env() -> dict[str, str] | None:
    """El entorno de vcvars64.bat (INCLUDE, LIB, PATH…), o nada si no está."""
    if not VCVARS.exists():
        return None
    r = subprocess.run(f'"{VCVARS}" >nul 2>&1 && set', shell=True, capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    env: dict[str, str] = {}
    for line in r.stdout.splitlines():
        key, sep, value = line.partition("=")
        if sep and key:
            env[key] = value
    return env if "INCLUDE" in env else None


def run(f: Path, fix: bool, build: Path, env: dict[str, str], extra: list[str]) -> tuple[Path, subprocess.CompletedProcess]:
    cmd = [str(CLANG_TIDY), "-p", str(build), "--quiet", *extra]
    if fix:
        cmd.append("--fix")
    r = subprocess.run([*cmd, str(f)], cwd=REPO, env=env, capture_output=True, text=True, encoding="utf-8",
                       errors="replace")
    return f, r


def main() -> int:
    args = [a for a in sys.argv[1:] if a != "--fix"]
    fix = "--fix" in sys.argv[1:]
    if not CLANG_TIDY.exists():
        print(f"No encuentro clang-tidy en {CLANG_TIDY} (CLION_HOME)")
        return 2
    configure()
    mingw_files = sources(BUILD)
    mingw_extra = ["--extra-arg=--target=x86_64-w64-mingw32",
                   "--extra-arg=-isystem", f"--extra-arg={REPO / 'tools' / 'dev' / 'tidy_shim'}",
                   "--extra-arg=-idirafter", f"--extra-arg={gcc_include()}"]
    jobs = [(f, BUILD, ENV, mingw_extra) for f in sorted(mingw_files)]
    # Lo que solo compila MSVC (con FFmpeg y Skia), con su base de compilación y su entorno.
    notes = []
    if (MSVC_BUILD / "compile_commands.json").exists():
        env = msvc_env()
        if env is None:
            notes.append(f"Sin {VCVARS} (VCVARS): gc_media, gc_render y gc_export no se analizan")
        else:
            jobs += [(f, MSVC_BUILD, env, []) for f in sorted(sources(MSVC_BUILD) - mingw_files)]
    else:
        notes.append(f"Sin {MSVC_BUILD} (tools/dev/verificar_msvc.bat): gc_media, gc_render y gc_export no se analizan")
    jobs = [j for j in jobs if not args or args[0] in str(j[0])]
    found: dict[tuple, str] = {}
    failed: list[str] = []
    # Con --fix, de uno en uno: dos clang-tidy podrían arreglar la misma cabecera a la vez.
    with concurrent.futures.ThreadPoolExecutor(1 if fix else os.cpu_count() or 4) as pool:
        for f, r in pool.map(lambda j: run(j[0], fix, j[1], j[2], j[3]), jobs):
            ours = 0
            for m in DIAG.finditer(r.stdout):
                path = Path(m["file"]).resolve()
                if not path.is_relative_to(REPO) or "_deps" in path.parts or "vcpkg_installed" in path.parts:
                    continue  # avisos de cabeceras de terceros o del sistema
                key = (str(path.relative_to(REPO)), int(m["line"]), int(m["col"]), m["check"])
                found[key] = m["msg"]
                ours += 1
            # clang-tidy falla (código ≠ 0) con un error de compilación. Si no es en nuestro código
            # (una cabecera de terceros, una opción que no entiende), el archivo no se ha analizado.
            if r.returncode != 0 and ours == 0:
                detail = (r.stderr.strip() or r.stdout.strip()).splitlines()[-3:]
                failed.append(f"{f.relative_to(REPO)}: clang-tidy salió con {r.returncode}: " + " | ".join(detail))
    for (path, line, col, check), msg in sorted(found.items()):
        print(f"{path}:{line}:{col}: {msg} [{check}]")
    by_check = collections.Counter(k[3] for k in found)
    for check, n in by_check.most_common():
        print(f"{n:5d}  {check}")
    for line in failed:
        print(f"SIN ANALIZAR {line}")
    for note in notes:
        print(f"AVISO: {note}")
    msvc_jobs = sum(1 for j in jobs if j[1] == MSVC_BUILD)
    print(f"{len(found)} avisos en {len(jobs)} archivos ({msvc_jobs} con la build de MSVC)"
          + (f", {len(failed)} sin analizar" if failed else ""))
    return 1 if found or failed else 0


if __name__ == "__main__":
    sys.exit(main())
