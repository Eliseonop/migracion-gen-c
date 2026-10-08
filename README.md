# GenClipPro

Motor C++ multiplataforma del editor de clips verticales. Es la migración de
[video-yt](E:/proyectos/video-yt) (FastAPI + React). El plan completo está en
[docs/PLAN_MIGRACION_CPP.md](docs/PLAN_MIGRACION_CPP.md).

**Estado:** F0 hecha · F1 modelo y formato hechos (ajustes, biblioteca y SFX de disco; faltan las colecciones) · F2 hecha: motor de edición (45 comandos: las 42 ops de video-yt,
animación por keyframes y máscaras, encuadre, pegar atributos, seguir objetos, sonorizar, animaciones de preset, recetas, subtítulos, figuras, efectos de catálogo, deshacer, grupos, historial en disco y autoguardado) · F3 en curso: la evaluación de un instante está hecha (animaciones, aparición y salida, fusión, ajustes de color, capas y el fotograma con la geometría de cada dibujo); `gc_media` y `gc_render` esperan al Windows SDK. Arquitectura: [docs/ARQUITECTURA.md](docs/ARQUITECTURA.md).
Lista de tareas de todas las fases: [docs/TODO.md](docs/TODO.md).

## Estructura

```
src/core/      gc_core: documento (modelo, JSON, migraciones) y motor de edición
               (comandos, EditSession, deshacer). Sin Qt, sin GPU.
apps/cli/      genclip: herramienta de línea de órdenes
resources/     recursos de la app (fx/catalog.json: catálogo de efectos y animaciones)
tests/         unit/ · parity/ (contra video-yt) · fixtures/
tools/         fixtures/export_fixtures.py (genera las fixtures desde video-yt)
cmake/         opciones y dependencias
docs/          plan de migración
```

## Compilar

Desde CLion: abre la carpeta, elige el perfil del preset **MinGW (CLion) · Debug**
(o **MSVC · Debug** cuando esté instalado el Windows SDK) y compila `gc_tests`.

Desde terminal:

```bash
cmake --preset mingw-debug
cmake --build --preset mingw-debug
ctest --preset mingw-debug
```

Atajo con el CMake de CLion: `bash tools/dev/verificar.sh [--limpio]` compila y pasa los tests en
Debug y Release, y falla si hay algún warning.

| Preset | Para qué |
|---|---|
| `mingw-debug` / `mingw-release` | MinGW de CLion. Provisional, solo para el núcleo (F0–F2). |
| `msvc-debug` / `msvc-release` | MSVC desde un *Developer Prompt* o desde CLion con el toolchain «Visual Studio». |
| `msvc-vcpkg-*` | MSVC + dependencias de vcpkg (necesita `VCPKG_ROOT`). Obligatorio desde F3 (FFmpeg, Skia, ORT). |
| `linux-debug`, `macos-debug` | Otras plataformas. |
| `ci` | Release con warnings como errores (GitHub Actions). |

Las dependencias (nlohmann/json, Catch2) se buscan primero con `find_package` y,
si no están, CMake las descarga y verifica su hash (`cmake/GcDependencies.cmake`).

## Paridad con video-yt

Durante la migración, video-yt es la referencia. Las fixtures se generan con su Python:

```bash
E:/proyectos/video-yt/.venv/Scripts/python.exe tools/fixtures/export_fixtures.py --real
```

- `tests/fixtures/model/synthetic.json`: casos de `tools/fixtures/model_cases.py`. Van al repo.
- `tests/fixtures/local/`: tus proyectos reales. Son datos personales: están en `.gitignore` y nunca se suben.

Si cambia `schemas.py` o `migrations.py` en video-yt, regenera las fixtures; el test
`paridad del modelo` dirá exactamente qué campo diverge.

## genclip

```bash
genclip projects E:/proyectos/video-yt/backend/data/projects.json   # valida todos los proyectos
genclip normalize <projects.json> <salida.json> [--preserve]          # reescribe normalizado
```
