# Changelog

Los cambios de GenClipPro, de la versión más nueva a la más vieja. El detalle de cada tanda (cómo se
verificó y con qué cifras) está en el §14 de [docs/PLAN_MIGRACION_CPP.md](docs/PLAN_MIGRACION_CPP.md); lo
pendiente, en [docs/TODO.md](docs/TODO.md).

La versión está en `CMakeLists.txt` (`project(... VERSION)`) y la dice `genclip --version`. Mientras dure la
migración (0.x):

- **menor** (0.**N**.0): cada tanda que añade algo que se puede usar;
- **parche** (0.N.**P**): arreglos, revisiones y cambios sin nada nuevo;
- **1.0.0**: cuando se retire la app Python (F9).

## [0.22.0] — 2026-11-26

### Añadido

- **Elegir el yt-dlp de «Directo»** (Vídeo › Directo de `genclip-studio`): abajo del panel se ve con qué yt-dlp se
  busca el directo, o que falta, y «Elegir…» (o «Cambiar…») abre el selector para indicar uno que ya tengas, como el
  de la venv de video-yt. Se recuerda para la próxima vez.
- **Se ve la versión** de `genclip-studio` (la de `CMakeLists.txt`): en el título de la ventana, abajo del inicio
  (debajo de dónde se guardan los proyectos) y arriba de Ajustes. `genclip-studio --version` también la dice.

### Arreglado

- «Directo» ya no depende de la carpeta de los proyectos para encontrar yt-dlp. Sin `--root` (con la carpeta de
  siempre, desde la 0.21.0) decía «No encuentro yt-dlp» aunque estuviera en la venv de video-yt. Ahora lo busca, por
  orden, en `GENCLIP_YTDLP`, el elegido, el PATH, el de winget (también si se acaba de instalar con el studio
  abierto) y la venv de la carpeta de los proyectos; si no hay ninguno, el aviso dice cómo tenerlo.
- Las casillas de «Cada marca guarda» cortaban el número (enseñaban «0» con 30 y 10): ahora son compactas y se lee
  entero.

## [0.21.0] — 2026-11-25

### Añadido

- **Elegir dónde se guardan los proyectos** (Ajustes › Carpeta de los proyectos de `genclip-studio`; en video-yt es
  fija): «Cambiar…» abre el selector de carpetas y el studio pasa al momento a abrir y guardar los proyectos de la
  nueva, con sus vídeos (`clips/`) y sus ajustes (`data/`). No se mueve nada. Se recuerda para la próxima vez, así que
  ya no hace falta arrancar con `--root` (si se pasa, sigue mandando). Si eliges la carpeta de video-yt, entra por su
  `backend/` y ves los mismos proyectos que allí. «Usar la de siempre» vuelve a la carpeta de datos de la app y «Abrir
  la carpeta» la enseña. En el inicio, debajo de las carpetas, se ve dónde están guardados; un clic lleva a Ajustes.
- Se cambia con el editor cerrado y sin una grabación de «Directo» ni una exportación en marcha. Si al arrancar la
  carpeta guardada no está (un disco desconectado), se abre la de siempre y se avisa.

### Sin hacer todavía

- Lo que aún hace el backend de video-yt por el puente (cargar de YouTube, voces, subtítulos y «Eliminar fondo») usa
  su propia carpeta: con los proyectos de otra, no los encuentra.

## [0.20.0] — 2026-11-25

### Añadido

- **Grabar un directo de TikTok o de YouTube y sacar clips** (Vídeo › Directo del material; es de GenClipPro, video-yt
  no lo tiene): pegas el enlace (tiktok.com/@usuario/live, el perfil o uno de YouTube) y «Grabar» lo graba desde ese
  momento, sin recomprimir, con el tiempo y el tamaño a la vista. «Marcar momento» (o Ctrl+M) apunta el instante; al
  parar, la grabación entra al material y cada marca deja un clip por referencia (30 s antes y 10 s después, o lo que
  digas en el panel). Si el directo acaba solo, se guarda igual. «Descartar» lo tira.
- yt-dlp encuentra el directo y FFmpeg lo graba: se buscan en `GENCLIP_YTDLP` y `GENCLIP_FFMPEG`, en el PATH y, yt-dlp,
  en la venv de video-yt si la carpeta de datos es su `backend/`.

### Sin hacer todavía

- TikTok solo deja grabar mientras el directo está en marcha (al acabar no queda grabación pública), y la grabación
  empieza al pulsar: lo anterior del directo no está. Las propuestas automáticas de momentos (picos del chat, del
  volumen o frases clave) llegarán con F6 y F7.

## [0.19.0] — 2026-11-25

### Añadido

- **Barras de cine en la pestaña Efectos de `genclip-studio`** (el `barsRow` de EdFxLibrary.jsx y `addCinemaBars`):
  un clic por proporción (2,39:1, 2:1, 1,85:1 o 16:9) pone una figura letterbox de 0 al final de la timeline, encima
  de todo, con el grosor que deja lo visible en esa proporción según el formato del proyecto; queda elegida para
  ajustar el grosor y la entrada en el inspector. Si el formato ya es más ancho, lo avisa.

### Cambiado

- La pestaña Efectos del material es una sola lista que se desplaza, en el orden de video-yt: los efectos animados,
  las recetas (ya sin plegar), la capa de ajuste y las barras de cine.

### Corregido

- **Una receta que deja «Eliminar fondo» por calcular lo calcula** (`startBgAutoFor` en `applyRecipeUI`): con «Sujeto
  que se adelanta», el recorte del sujeto se pide al momento por el puente, como en video-yt; antes la copia se
  quedaba sin recortar. La selección pasa a lo primero que añade la receta en el orden de la timeline.
- **`genclip-studio` vuelve a compilar con `gc_media` (MSVC):** desde la 0.17.0, la tarjeta de un stick trataba la
  duración de `probe_duration` como opcional (`value_or`), y es un `double`. Las versiones 0.14.0–0.18.0 solo se
  habían compilado en Linux sin FFmpeg, donde esa rama no entra.
- **`main` reconciliado con GitHub:** entre el 18 y el 20 de noviembre, las recetas, «Editar información», «Nueva
  imagen» y los sticks se hicieron dos veces, en local y en una sesión en la nube. Se quedan las versiones de 0.14.0 a
  0.18.0, más fieles a video-yt y con tests; de las locales vienen las barras de cine, el recorte tras una receta y el
  orden de la pestaña Efectos. Las locales siguen en el historial de git.

## [0.18.0] — 2026-11-20

### Añadido

- **«Agregar Stick» en la timeline de `genclip-studio`** (EdStickMenu.jsx): clic derecho en un hueco → personaje →
  vídeo o imagen → expresión; el clip entra en ese instante y esa pista (o en la de vídeo), sin pisar a otro y con el
  croma del personaje ya activado.

## [0.17.0] — 2026-11-20

### Añadido

- **Personajes stick en `genclip-studio`** (EdLibraryStick.jsx y el «Nuevo stick» de EdLibrary.jsx): en
  Biblioteca › Colecciones, un stick se abre agrupado por expresión (feliz, triste…), con «Subir» por expresión,
  tarjetas que se arrastran a la timeline o se añaden con «+», y un selector para cambiarlas de expresión. «Nuevo
  stick» en «Gestionar carpetas» (nombre, emoji y color del croma).

### Corregido

- **Un stick soltado nace con su croma** (`place_asset_clip`, la rama `chromaBg` de `placeAsset` que faltaba en
  `gc_core`), también desde la búsqueda de colecciones.

## [0.16.0] — 2026-11-20

### Añadido

- **«Nueva imagen» en `genclip-studio`** (ImageAddModal.jsx): el + de la pestaña Imagen del material abre una lista
  de imágenes elegidas, cada una con su vista y su descripción, que se suben al material del proyecto de una vez
  (y se pueden quitar de la lista antes de guardar).

## [0.15.0] — 2026-11-20

### Añadido

- **Recetas en un clic en `genclip-studio`** (EdRecipes.jsx): en la pestaña Efectos del material, las cinco
  recetas de video-yt (etalonaje de cine, texto con reflejo, texto que atraviesas, franjas al ritmo y sujeto que se
  adelanta), cada una activa solo si lo seleccionado basta (`recipeReady`), con el motivo si no. Se aplican sobre la
  selección y se deshacen de una vez.
- **El catálogo de recetas y `recipe_ready` en `gc_core`** (`recipes.hpp`), con paridad contra `recipes.py` y
  `recipeReady.js`.
- `tools/dev/mutantes.py` funciona también fuera de Windows (build `ci`).

## [0.14.0] — 2026-11-20

### Añadido

- **«Editar información» del material en `genclip-studio`** (MaterialInfoModal.jsx): clic derecho en un vídeo, una
  imagen o un audio, del proyecto o guardado, para cambiar su título y su descripción (lo que lee la IA para elegir el
  material), con la descripción de la IA a un clic. Ctrl+Enter guarda.

### Corregido

- **Compila y pasa en Linux con GCC 13:** faltaba `#include <format>`, cuatro tests recorrían un subobjeto de un
  temporal (`store.load()["projects"]`, que MSVC alarga y GCC 13 no), y los tests que necesitan `gc_media` (importar
  JPG y vídeos) se saltan sin él. 271 tests en verde en Linux (sin FFmpeg ni Skia).

## [0.13.0] — 2026-11-18

### Añadido

- **El Clip Editor en `genclip-studio`:** «Preparar clip» abre un vídeo del proyecto entero (o un segmento, con su
  rango marcado); Z y X marcan el inicio y el fin (o se arrastran en la regla), y «Crear clip» o «Agregar a material»
  lo guardan en el material por referencia, sin render, con su título y su descripción. Cada pestaña conserva su
  timeline, y la del Clip Editor no se guarda en el proyecto.
- **El extractor del Clip Editor en `gc_core`** (`clip_extract.hpp`), con paridad contra `clipExtract.js` de
  video-yt.
- **El editor se parece a video-yt** (2026-11-16 y 17): barra superior, reproductor con el formato de salida, la barra
  de iconos del material con tarjetas y miniaturas, el + de las tarjetas, el panel de Keyframes, la barra de la
  timeline entera (añadir pistas, imán, igualar duración, congelar fotograma y separar solapes), el volumen de escucha
  y el JSON del proyecto.

### Corregido

- El test de «separar solapes» del studio ya no revienta: pedía `isVisible()` a un botón oculto.

## [0.12.0] — 2026-11-14

### Añadido

- **El pack de un tramo en la dirección de escena** (`scene_direction.hpp`): todo lo que ve la IA para decidir qué se
  muestra en un tramo (el guion con la frase de antes y la de después, los subtítulos y las palabras con tiempos, la
  dirección, el material elegido o los candidatos, la referencia y los vecinos, lo que ya ocupa la timeline, el
  material usado hace poco, la composición planificada, el estilo y la dirección global), su texto exacto, los tokens
  estimados, los beats según el ritmo y el arranque del brief de Generar Escena, con paridad contra
  `scene_direction.py` de video-yt.
- **Qué ocupa ya la timeline en un tramo y el estilo de sus subtítulos** (`segment_context.hpp`): `timeline_context` y
  `style_context` de `motion/segment_context.py`.
- **Una tool más en el servidor MCP** (58 de 100): `scene_direction_pack`.

### Corregido

- `scene_rank_materials` con un `limit` que es un texto con un entero de más de 64 bits da 20 (o 1 si es negativo),
  como Python; antes daba 5.

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
