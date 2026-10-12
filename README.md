# GenClipPro

Motor C++ multiplataforma del editor de clips verticales. Es la migración de
[video-yt](E:/proyectos/video-yt) (FastAPI + React). El plan completo está en
[docs/PLAN_MIGRACION_CPP.md](docs/PLAN_MIGRACION_CPP.md).

**Estado:** F0 hecha salvo el CI en marcha (falta el primer push); MSVC 14.50 compila y pasa los tests · F1 hecha: modelo y formato (también NaN e Infinity como Python), ajustes, biblioteca con «Guardar en la biblioteca», SFX de disco, colecciones de material y el almacén de proyectos (dar de alta SFX llega con F5) · F2 hecha: motor de edición (45 comandos: las 42 ops de video-yt,
animación por keyframes y máscaras, encuadre, pegar atributos, seguir objetos, sonorizar, animaciones de preset, recetas, subtítulos, figuras, efectos de catálogo, deshacer, grupos, historial en disco, autoguardado y la envolvente de un sonido) · F3 en curso: la evaluación de un instante (animaciones, aparición y salida, fusión, ajustes de color, capas y la geometría de cada dibujo), `gc_media` con FFmpeg (sondas, decode y seek como el export, VP9 con alfa y AV1) y `gc_render` con Skia (imágenes, vídeos y GIF en overlay, croma, textos quietos con HarfBuzz, filtros de color y máscaras; `genclip frame` saca el fotograma compuesto de un proyecto real); las cadenas de audio del export ya salen iguales y faltan ejecutarlas, el encode y `genclip render`. Arquitectura: [docs/ARQUITECTURA.md](docs/ARQUITECTURA.md).
Lista de tareas de todas las fases: [docs/TODO.md](docs/TODO.md).

## Estructura

```
src/core/      gc_core: documento (modelo, JSON, migraciones) y motor de edición
               (comandos, EditSession, deshacer). Sin Qt, sin GPU.
src/log/       gc_log: registro sobre spdlog (ejecutables y capas con E/S; gc_core no lo usa)
apps/cli/      genclip: herramienta de línea de órdenes
resources/     recursos de la app (fx/catalog.json: catálogo de efectos y animaciones)
tests/         unit/ · parity/ (contra video-yt) · fixtures/
tools/         fixtures/export_fixtures.py (genera las fixtures desde video-yt)
cmake/         opciones y dependencias
docs/          plan de migración
```

## Compilar

Desde CLion: abre la carpeta, elige el perfil del preset **MinGW (CLion) · Debug**
(o **MSVC · Debug**) y compila `gc_tests`.

Desde terminal:

```bash
cmake --preset mingw-debug
cmake --build --preset mingw-debug
ctest --preset mingw-debug
```

Atajo con el CMake de CLion: `bash tools/dev/verificar.sh [--limpio]` compila y pasa los tests en
Debug y Release, y falla si hay algún warning.

Análisis estático: `python tools/dev/tidy.py [--fix] [filtro]` pasa el clang-tidy de CLion con el
`.clang-tidy` del repo por `src`, `apps` y `tests`, y falla si hay algún aviso (tiene que quedar en 0).

| Preset | Para qué |
|---|---|
| `mingw-debug` / `mingw-release` | MinGW de CLion. Provisional, solo para el núcleo (F0–F2). |
| `msvc-debug` / `msvc-release` | MSVC desde un *Developer Prompt* o desde CLion con el toolchain «Visual Studio». |
| `msvc-vcpkg-*` | MSVC + dependencias de vcpkg (necesita `VCPKG_ROOT`). Obligatorio desde F3 (FFmpeg, Skia, ORT). |
| `linux-debug`, `macos-debug` | Otras plataformas. |
| `ci` | Release con warnings como errores (GitHub Actions). |

Las dependencias (nlohmann/json, spdlog, Catch2) se buscan primero con `find_package` y,
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
genclip frame E:/proyectos/video-yt/backend <proyecto> --at 5 -o f.png [--omitir]  # fotograma compuesto (con FFmpeg y Skia)
```

Los errores van a stderr («error: …»). `GENCLIP_LOG=debug` (o `trace`, `info`, `warn`, `error`,
`off`) cambia el nivel del registro; por defecto, `warn`.
