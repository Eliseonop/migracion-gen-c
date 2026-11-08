#!/usr/bin/env bash
# Compila y pasa los tests con el CMake y la MinGW de CLion.
#
#   bash tools/dev/verificar.sh [--limpio] [preset...]
#
# Por defecto: mingw-debug y mingw-release. Con --limpio recompila todo (así salen
# todos los warnings, no solo los de los archivos que cambiaron).
# Sale con error si falla la compilación, falla algún test o aparece algún warning.
set -uo pipefail

# CLION_HOME o, si no está, el CLion más nuevo de Program Files/JetBrains.
CLION="${CLION_HOME:-$(ls -d "/c/Program Files/JetBrains/CLion "* 2>/dev/null | sort -V | tail -n 1)}"
export PATH="$CLION/bin/mingw/bin:$CLION/bin/ninja/win/x64:$CLION/bin/cmake/win/x64/bin:$PATH"
cd "$(dirname "$0")/../.." || exit 1
# La consola en UTF-8: CMake lee con su página de códigos la lista de tests de Catch2 y, con la
# 850 o la 1252 de un Windows sin «UTF-8 para todo el mundo», estropea las tildes de los nombres
# y ctest ya no los encuentra.
command -v chcp.com >/dev/null && chcp.com 65001 >/dev/null

limpio=()
presets=()
for a in "$@"; do
  case "$a" in
    --limpio) limpio=(--clean-first) ;;
    *) presets+=("$a") ;;
  esac
done
[ ${#presets[@]} -eq 0 ] && presets=(mingw-debug mingw-release)

status=0
for p in "${presets[@]}"; do
  dir="build/$p"
  echo "== $p"
  if [ ! -f "$dir/CMakeCache.txt" ]; then
    cmake --preset "$p" >/dev/null || { echo "CONFIGURACIÓN FALLIDA"; status=1; continue; }
  fi
  log=$(cmake --build "$dir" "${limpio[@]}" 2>&1)
  rc=$?
  warnings=$(grep -c 'warning:' <<<"$log")
  if [ $rc -ne 0 ]; then
    grep -E -A3 'error|warning:' <<<"$log" | head -60
    echo "COMPILACIÓN FALLIDA"
    status=1
    continue
  fi
  if [ "$warnings" -gt 0 ]; then
    grep -A3 'warning:' <<<"$log" | head -60
    echo "WARNINGS: $warnings"
    status=1
  fi
  # Con tope por test: uno colgado falla en vez de esperar siempre.
  ctest --test-dir "$dir" -j 8 --timeout 1800 --output-on-failure 2>&1 | tail -n 15 || status=1
done
exit $status
