# Changelog

Los cambios de GenClipPro, de la versión más nueva a la más vieja. El detalle de cada tanda (cómo se
verificó y con qué cifras) está en el §14 de [docs/PLAN_MIGRACION_CPP.md](docs/PLAN_MIGRACION_CPP.md); lo
pendiente, en [docs/TODO.md](docs/TODO.md).

La versión está en `CMakeLists.txt` (`project(... VERSION)`) y la dice `genclip --version`. Mientras dure la
migración (0.x):

- **menor** (0.**N**.0): cada tanda que añade algo que se puede usar;
- **parche** (0.N.**P**): arreglos, revisiones y cambios sin nada nuevo;
- **1.0.0**: cuando se retire la app Python (F9).

## [0.11.0] — 2026-11-14

### Añadido

- **El material en la dirección de escena** (`scene_direction.hpp`): el catálogo de vídeos e imágenes del proyecto
  y de la biblioteca, su metadata semántica (qué sale, qué pasa, el ambiente, el uso sugerido…) y el ranking de
  materiales por los términos clave de un texto, con paridad contra `scene_direction.py` de video-yt.
- **Tres tools más en el servidor MCP** (57 de 100): `scene_direction_get`, `scene_rank_materials` y
  `scene_set_material_meta`.

### Cambiado

- El `\w` de las regex de Python (`py_word_char`) es exacto en todo Unicode (antes, aproximado fuera de Latin-1).

## [0.10.0] — 2026-11-13

### Añadido

- **La dirección de escena en el núcleo** (`scene_direction.hpp`): la escaleta de tramos del guion, la dirección
  visual global del vídeo, el guion en frases con tiempos (de los subtítulos, de la transcripción del material o
  estimado desde el texto del audio), la escaleta propuesta y el estado del montaje, con paridad contra
  `scene_direction.py` de video-yt.
- **Seis tools más en el servidor MCP** (54 de 100): `scene_get_blueprint`, `scene_set_blueprint`,
  `scene_direction_status`, `scene_direction_auto_split`, `scene_direction_update_segment` y `scene_direction_set`.
  Sus errores llegan al agente con el código, el parámetro y si se puede reintentar, como en video-yt.

## [0.9.0] — 2026-11-12

### Añadido

- **Cuatro tools más en el servidor MCP** (48 de 100): `replace_media_version` (las otras versiones de un vídeo
  de YouTube o cambiarlas en la timeline, en un paso de deshacer), `delete_media` (el material y su archivo),
  `search_sfx` (la biblioteca de SFX) y `set_clip_ai_description` (la descripción de la IA, aparte de la del
  usuario), con paridad contra las tools de video-yt. Los errores con pista (`hint`) llegan al agente como en
  video-yt.
- `genclip mcp` mide los SFX y los vídeos con gc_media (antes, un SFX duraba 3 s y no se comprobaba la
  proporción de un vídeo).

## [0.8.0] — 2026-11-12

### Añadido

- **«Subir calidad»**: «Usar esta calidad en la timeline» en el menú del clic derecho de un vídeo del proyecto que
  viene de YouTube y, al acabar de agregar el vídeo completo, la misma oferta si la timeline usaba una versión peor.
  El modal dice qué versiones y cuántos clips cambian; al confirmar, los clips pasan al archivo nuevo conservando
  cortes, encuadre y animaciones (en un paso de deshacer) y los tramos por referencia también. Va en C++, sin el
  backend: la búsqueda de versiones y el cambio son los de `media_upgrade.hpp`.
- En `gc/core/yt_ui.hpp`: cuándo se ofrece (isYtUrl y canUpgradeFrom), el texto de la pregunta y los avisos, con
  paridad contra el JS real y sus mutaciones.

## [0.7.0] — 2026-11-11

### Añadido

- **Pestaña «YouTube» del material** («Cargar clips»): pegar un enlace y analizarlo por el backend (título,
  duración, tramos recomendados por el heatmap y calidades), el historial de enlaces (abrir uno o quitarlo) y
  **agregar el vídeo completo** al material en la mejor calidad o en la que se elija, con su progreso y cancelable.
- `gc/core/yt_ui.hpp`: las calidades y sus textos (ytQuality.js) y el historial (normalizeHistory y removeHistory),
  con paridad contra el JS real y sus mutaciones.
- Con esto, todo lo del puente con el backend que usa el editor (subtítulos, «Seguir cara», narrador, audio de un
  enlace, «Eliminar fondo», seguimiento de caras y YouTube) ya está en el studio.

### Sin hacer todavía

- Editar un tramo recomendado o un rango propio (abre el Clip Editor, que el studio aún no tiene), su vista previa
  embebida y «Subir calidad» cuando la timeline usa una versión peor del vídeo descargado.

## [0.6.0] — 2026-11-11

### Añadido

- **«Seguimiento de caras» del material** (clic derecho en un vídeo del proyecto): el backend analiza la cara del
  vídeo (o reutiliza el análisis guardado; «Volver a analizar caras» lo repite) y el encuadre de sus clips de la
  timeline pasa a seguirla, en un paso de deshacer. `apply_face_track` y `face_track_clip` en
  `gc/core/face_follow.hpp`, con paridad contra `applyFaceTrack` y `applyFaceTrackToClips` reales y sus mutaciones.

## [0.5.0] — 2026-11-11

### Añadido

- **Eliminación personalizada (SAM) en la pestaña «Fondo»:** pincel y borrador inteligentes y normales sobre la vista
  previa, con el clip sin recortar y la selección en cian encima (POST `/bg-segment` al soltar el trazo), análisis
  del clip en segundo plano (`/bg-analyze`), los fotogramas marcados para saltar a ellos, «Último trazo», «Limpiar»,
  calidad del modelo y «Aplicar a todo el clip».
- **Retocar máscara:** Conservar y Eliminar sobre el matte de la automática, que se ve en el recorte mientras se pinta.
- Con esto, «Eliminar fondo» queda entero en el studio.
- `gc/core/bg_ui.hpp`: toggleBgCustom, los trazos, las marcas y el paso del cuadro a la fuente (por la inversa del
  blit), con paridad contra el JS real, también contra `canvasToSourceNorm` y `bgBrushRadiusPx`, y sus mutaciones.
- Barra de desplazamiento en el inspector.

## [0.4.0] — 2026-11-11

### Añadido

- **«Exportar recorte»** en la pestaña «Fondo», para vídeos y GIF con matte listo o croma: hornea el clip a un WebM
  transparente en el backend (POST `/api/projects/{id}/bg-cutout`, cancelable) y aparece en el material.
  `bg_cutout_body` en `gc/core/bg_ui.hpp`, con paridad contra `exportBgCutout` real y sus mutaciones.

## [0.3.0] — 2026-11-10

### Añadido

- **Chroma key en la pestaña «Fondo»:** activar, color (muestra con el selector, #RRGGBB o los cuatro preestablecidos),
  **cuentagotas** (toma el píxel compuesto bajo el clic en la vista previa), tolerancia, suavizado, derrame, limpiar
  borde, expandir y restablecer.
- **Contorno del recorte:** activar, color, grosor, difuminado (halo) y opacidad.
- **Fondo de vista previa:** normal, cuadros, color, o una imagen o un vídeo de prueba (que se repite). Solo en la
  vista previa: `FrameOptions::backdrop` y `render::Backdrop`, que el export no usa.
- `bg_patch_chroma`, `bg_patch_outline` y `bg_reset_chroma` en `gc/core/bg_ui.hpp`, con paridad contra el
  VideoEditor.jsx real (500 llamadas en total en el caso de «Eliminar fondo») y sus mutaciones.

## [0.2.0] — 2026-11-10

### Añadido

- **«Eliminar fondo» en el studio (F5, por el puente con el backend de video-yt).** Pestaña «Fondo» del
  inspector para vídeos e imágenes, como la «Eliminación automática» de EdBgRemove.jsx: activarla (propone
  RVM en un vídeo, BiRefNet en una imagen y U²-Net en un GIF, con su alto de matte), elegir motor y detalle,
  estabilizar, **Aplicar / Recalcular** (POST `/api/projects/{id}/bg-removal`, sondeado cada 700 ms),
  **Cancelar** (DELETE `/api/job/{id}`) y el estado (calculando, error, aviso del motor o listo). Con el matte
  listo, **Ajustar recorte**: invertir, umbral, suavizado, pluma, expandir y opacidad, al instante y sin
  volver a ejecutar el modelo. Los motores y dónde se ejecutan salen de GET `/api/bg/providers`. Cada cambio
  es un paso de deshacer y la vista previa lee el matte de `data/bgcache`.
- `gc/core/bg_ui.hpp`: lo que hace el editor con `clip.bg_removal` (toggleBgAuto, changeBgAuto,
  startBgAutoFor con su cuerpo y el final del job), con paridad contra el VideoEditor.jsx real (270 llamadas
  en `--solo editor`) y sus mutaciones (`mutantes.py bg_ui`).
- `BackendJobs::cancel`.
- Este CHANGELOG y la regla de versiones.

### Sin hacer todavía

- La Eliminación personalizada (SAM y sus pinceles), retocar la máscara, «Exportar recorte» y el croma del
  inspector.
- Probarlo contra el backend real: el test sin pantalla usa uno falso.

## [0.1.0] — 2026-10-07 a 2026-11-09

Todo lo anterior a este archivo, por fases (el detalle, en el §14 del plan):

- **F0, cimientos:** CMake con presets (MinGW, MSVC, MSVC + vcpkg, Linux, macOS y CI), avisos estrictos,
  clang-tidy, spdlog y el CI escrito.
- **F1, modelo y formato:** el modelo de `schemas.py` con sus migraciones, ajustes y claves, biblioteca,
  SFX en disco, colecciones, personajes stick, almacén de proyectos, segmentos y JSON como Python.
- **F2, edición:** las 42 operaciones, sin solapes, keyframes, historial en disco y autoguardado.
- **F3 y F4, render y export:** `gc_media`, `gc_render` y `genclip render` de punta a punta, los 42 efectos
  de vídeo, el croma con contorno, el matte de «Eliminar fondo» desde la caché y la medida del texto como
  Pillow.
- **F5, el studio (Qt Quick):** inicio, timeline, material, inspector, ajustes, vista previa con el
  fotograma del export, reproductor con el reloj del audio, export con progreso, máscara y pincel, y por el
  puente con el backend: subtítulos, «Seguir cara» y «Cargar audio».
- **F6, IA local (sin ONNX Runtime):** beats, presupuesto de RAM de los modelos y lo de alrededor del
  detector de caras.
- **F7, nube y MCP:** las piezas sin red de YouTube y el servidor MCP por HTTP (`genclip mcp`) con 44 de sus
  100 tools.
