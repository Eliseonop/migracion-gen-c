"""clang-tidy (con el .clang-tidy del repo) sobre todo el código: src, apps y tests.

    python tools/dev/tidy.py [--fix] [filtro]

`filtro`: solo los archivos cuya ruta contiene ese texto. `--fix`: aplica los arreglos que
clang-tidy sabe hacer (revisa el diff y compila después). Sale con error si hay algún aviso.

Usa el clang-tidy y la MinGW de CLion con una build propia, build/tidy (mingw-debug sin el
escaneo de módulos de C++20, cuyas opciones de GCC clang no entiende). Ese clang-tidy no
trae las cabeceras internas de clang: las de GCC sirven (-idirafter), salvo las de
intrínsecos x86, que usan builtins de GCC; windows.h las incluye y se sustituyen por las
vacías de tools/dev/tidy_shim (nuestro código no usa intrínsecos).
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
CLION = Path(os.environ.get("CLION_HOME", r"C:\Program Files\JetBrains\CLion 2026.2.3"))
CLANG_TIDY = CLION / "bin" / "clang" / "win" / "x64" / "bin" / "clang-tidy.exe"
MINGW = CLION / "bin" / "mingw"
ENV = dict(os.environ)
ENV["PATH"] = os.pathsep.join([str(MINGW / "bin"), str(CLION / "bin" / "ninja" / "win" / "x64"),
                               str(CLION / "bin" / "cmake" / "win" / "x64" / "bin"), ENV["PATH"]])
BUILD = REPO / "build" / "tidy"
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


def sources() -> list[Path]:
    db = json.loads((BUILD / "compile_commands.json").read_text(encoding="utf-8"))
    out = set()
    for entry in db:
        f = Path(entry["file"]).resolve()
        rel = f.relative_to(REPO) if f.is_relative_to(REPO) else None
        if rel is not None and rel.parts[0] in ("src", "apps", "tests"):
            out.add(f)
    return sorted(out)


def run(f: Path, fix: bool) -> tuple[Path, subprocess.CompletedProcess]:
    cmd = [str(CLANG_TIDY), "-p", str(BUILD), "--quiet",
           "--extra-arg=--target=x86_64-w64-mingw32",
           "--extra-arg=-isystem", f"--extra-arg={REPO / 'tools' / 'dev' / 'tidy_shim'}",
           "--extra-arg=-idirafter", f"--extra-arg={gcc_include()}"]
    if fix:
        cmd.append("--fix")
    r = subprocess.run([*cmd, str(f)], cwd=REPO, env=ENV, capture_output=True, text=True, encoding="utf-8",
                       errors="replace")
    return f, r


def main() -> int:
    args = [a for a in sys.argv[1:] if a != "--fix"]
    fix = "--fix" in sys.argv[1:]
    if not CLANG_TIDY.exists():
        print(f"No encuentro clang-tidy en {CLANG_TIDY} (CLION_HOME)")
        return 2
    configure()
    files = [f for f in sources() if not args or args[0] in str(f)]
    found: dict[tuple, str] = {}
    failed: list[str] = []
    # Con --fix, de uno en uno: dos clang-tidy podrían arreglar la misma cabecera a la vez.
    with concurrent.futures.ThreadPoolExecutor(1 if fix else os.cpu_count() or 4) as pool:
        for f, r in pool.map(lambda f: run(f, fix), files):
            ours = 0
            for m in DIAG.finditer(r.stdout):
                path = Path(m["file"]).resolve()
                if not path.is_relative_to(REPO) or "_deps" in path.parts:
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
    print(f"{len(found)} avisos en {len(files)} archivos" + (f", {len(failed)} sin analizar" if failed else ""))
    return 1 if found or failed else 0


if __name__ == "__main__":
    sys.exit(main())
