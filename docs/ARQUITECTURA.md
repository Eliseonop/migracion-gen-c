# Arquitectura de GenClipPro

Cómo está construido el motor y por qué, con la vara de un editor profesional
(Premiere, DaVinci, CapCut): edición instantánea, deshacer fiable, preview igual al
export y una UI que nunca se bloquea. El plan por fases está en
[PLAN_MIGRACION_CPP.md](PLAN_MIGRACION_CPP.md).

## 1. Principios

1. **El documento es un valor inmutable.** La timeline no se modifica nunca en el sitio: cada edición produce una timeline nueva que comparte con la anterior todo lo que no cambió. El hilo de render, el export o el autosave pueden quedarse un *snapshot* sin miedo a verlo a medio editar.
2. **Toda edición es un comando y entra por una sola puerta.** La UI, el servidor MCP, los scripts y los tests construyen el mismo `Command` y lo pasan por el mismo pipeline. No hay atajos que se salten las reglas (sin solapes, validación).
3. **Las operaciones son puras y deterministas.** Reciben `const Timeline&`, devuelven un resultado y no tocan nada más. Los ids se piden a un `IdSource` inyectable. Así cada operación se puede probar, repetir y comparar con video-yt.
4. **Preview = export.** El mismo código decide qué se ve en el instante *t* (evaluación) y lo dibuja (render). Cambia solo la resolución y el destino (pantalla o encoder). *(F3)*
5. **La UI nunca espera.** Decodificar, renderizar, la IA y el export van en otros hilos y se comunican con snapshots inmutables y colas. El hilo de audio no reserva memoria ni toma locks. *(F3)*
6. **Los resultados asíncronos vuelven como comandos.** Cuando un job termina (quitar fondo, transcribir…), su resultado entra al documento como un comando más, así que se puede deshacer y respeta las reglas.

## 2. Capas

```
┌──────────────────────────────────────────────────────────────────────┐
│ UI (Qt Quick / QML)        ViewModels: observan EditSession y         │  F5
│                            envían comandos; nunca tocan el modelo      │
├──────────────────────────────────────────────────────────────────────┤
│ Servicios                  proyectos, biblioteca, jobs, IA, MCP        │  F5–F7
├────────────────────────────────── gc_core ───────────────────────────┤
│ EditSession                documento vivo: snapshot actual, historial, │  ✅
│                            grupos, observadores, revisión              │
│ Motor de edición           comando → operación pura → sin solapes →    │  ✅ 44 ops
│                            validación                                  │
│ Animación                  keyframes (curvas, bézier, colores),        │  ✅
│                            máscaras, volumen: valores en el instante t │
│ Reglas de timeline         tipos de clip, duraciones, pila de capas,   │  ✅
│                            validación                                  │
│ Documento                  modelo, JSON (compatible con video-yt),     │  ✅
│                            migraciones                                 │
├────────────────────────────────── F3 ────────────────────────────────┤
│ Evaluación                 evaluate(snapshot, t) → FrameDesc           │
│ Render (gc_render, Skia)   FrameDesc → imagen (preview ½ / export 1:1) │
│ Media (gc_media, FFmpeg)   decodificar con caché y proxies, audio,     │
│                            codificar                                   │
│ Reproducción               reloj de audio, lectura anticipada          │
│ Export                     evaluar → render → encode, en tubería       │
└──────────────────────────────────────────────────────────────────────┘
```

Las dependencias van solo hacia abajo: `gc_core` no sabe que existen Qt, la GPU ni FFmpeg, y por eso corre igual en Windows, macOS, Linux, Android e iOS.

## 3. Documento

| Pieza | Archivo | Qué hace |
|---|---|---|
| Modelo | `gc/core/model.hpp` | Espejo de `schemas.py`: mismos campos, orden y valores por defecto. |
| JSON | `gc/core/serialize.hpp` | Lee con las coerciones de Pydantic v2. Escribe en dos modos: `Model` (= `model_dump()`) y `Preserve` (conserva las claves desconocidas). |
| Migraciones | `gc/core/migrations.hpp` | De `schema_version` 1 a 5, sobre el JSON crudo, antes de validar. |
| Estructura compartida | `gc/core/cow.hpp` | `Timeline::clips` es `vector<Cow<TimelineClip>>`. |

**Por qué `Cow<T>` (copy-on-write):**
- Copiar una timeline copia punteros, no clips.
- `mut()` clona un clip solo si alguien más lo está viendo.
- Un `move_clip` en una timeline de 167 clips copia **un** clip; los otros 166 son el mismo objeto en el antes y el después.
- Por eso guardar 50 pasos de deshacer cuesta casi lo mismo que guardar uno.
- `touched_clip_ids` descarta al instante los clips que son el mismo objeto.

**Tiempo.** De momento, segundos en `double`, como el JSON de video-yt. Es imprescindible para la paridad exacta: los redondeos usan `py_round`, el `round()` de Python bit a bit. Cuando se retire Python (M3) conviene pasar a tiempo entero (ticks o microsegundos, como CapCut y Premiere) con una migración `schema_version` 6. Los fotogramas quedarían exactos y no habría deriva de coma flotante.

## 4. Motor de edición

```
parse_command(op, params) ──► Command (std::variant de structs)
                                   │
apply_command(timeline, command, ids):
   1. run_command      operación pura                 (edit_ops.cpp)
   2. settle_timeline  ninguna pista con dos clips a la vez:
                       crece → empuja; cae encima → sube de pista;
                       pista magnética → sin huecos   (no_overlap.cpp)
   3. validate         rechaza solo errores NUEVOS    (timeline_rules.cpp)
   ──► EditResult { timeline, changed, warnings }
```

- **Comandos disponibles (44):**

  | Grupo | Comandos |
  |---|---|
  | Pistas | `add_track`, `reorder_track`, `rename_track`, `remove_track`, `link_tracks`, `unlink_track` |
  | Clips | `add_clip`, `move_clip`, `remove_clip`, `split_clip`, `duplicate_clip`, `freeze_frame` |
  | Propiedades | `update_clip` (varias en un paso), `set_clip_opacity`, `set_clip_speed`, `set_clip_transition`, `set_text_role`, `set_clip_blend`, `set_clip_flip`, `set_clip_layout`, `set_clip_note` |
  | Encuadre | `reframe_clip`, `crop_clip` |
  | Animación y efectos | `set_clip_keyframes`, `set_clip_beats`, `set_clip_masks`, `set_clip_effects`, `apply_effect`, `set_clip_animation`, `set_clip_bg_removal` |
  | Material nuevo | `add_subtitles` (desde la transcripción), `add_shape` (figuras y trazados), `add_cinema_bars`, `add_adjustment_layer` |
  | Audio | `set_clip_volume` (con fundidos), `set_track_audio`, `set_clip_audio_fx` (con rampa animada) |
  | Timeline y material | `set_project_format`, `set_markers`, `replace_media` |
  | Compuestos | `paste_clip_attributes`, `follow_object`, `add_sound_design`, `animate_clip` |

- **Módulos de datos que usan los comandos:** el render de F3 los reutiliza.

  | Cabecera | Qué contiene |
  |---|---|
  | `fx_catalog.hpp` | Catálogo de efectos y animaciones; recurso `resources/fx/catalog.json` |
  | `pose.hpp` | Pose del clip y encuadre en t |
  | `shapes.hpp` | Figuras normalizadas |
  | `bg.hpp` | Eliminar fondo normalizado |
  | `captions.hpp` | Fragmentación de subtítulos |
  | `clip_layout.hpp` | Encuadre: ventana de recorte, punto de la fuente → salida y rectángulo de destino |
  | `clip_attrs.hpp` | Grupos de atributos y «Pegar atributos», con la fusión de keyframes |
  | `follow.hpp` | Del recorrido de un objeto a los keyframes del clip que lo acompaña |
  | `clip_motion.hpp` | Presets de animación (zoom, giro, deslizar, fundido, pop, pulso, trazo) |

- **Parámetros:** los nombres son los del MCP de video-yt. `parse_command` aplica las reglas de los kwargs de Python: parámetro desconocido = error y obligatorio ausente = error.
- **Paridad:** 348 casos sintéticos (144 de error) y 1845 operaciones sobre tus 18 timelines reales coinciden con `timeline_store.apply_op` en timeline, `changed` y `warnings`, y fallan donde Python falla (el mensaje no se compara). Las reales incluyen subtítulos generados desde sus transcripciones y fotogramas congelados.
  - **Casos límite cubiertos:** el «crece → empuja» de `no_overlap` (un clip a velocidad 0,5 empuja a los de detrás) y el reparto de subtítulos solapados en pistas nuevas.
  - **Tipos de Python:** se respetan detalles como que un valor acotado a un límite entero salga entero (`min(1, max(0, 1.5))` es el int `1`).

### Animación (`gc/core/keyframes.hpp`, `gc/core/masks.hpp`)

- **`clip_props_at(clip, t)`:** las 31 propiedades animables en el instante *t*: pose, giro 3D, trazo, la máscara `masks[0]`, volumen y 11 efectos de audio. Cada propiedad se interpola entre los keyframes que la tienen, con su curva: lineal, ease, cúbicas, rebote, bézier propia o *hold*.
- **`text_style_at`:** el estilo de texto animado (colores en `#rrggbb` mezclados por canal, fondo, sombra, brillo…).
- **`clip_masks_at`:** las máscaras normalizadas en *t*.
- **`upsert_keyframe_at` / `apply_volume_fade`:** crean o editan snapshots; los usan los fundidos y las rampas de audio.
- **Paridad:** 236 casos sintéticos (clips, curvas sueltas, `normalize_mask`, estilos) y **733 clips reales** muestreados en sus keyframes y entre medias. Sin tolerancia coinciden bit a bit **25 676 de 25 677** comprobaciones; la otra difiere en el último bit de un `pow()` de libm, así que los números se comparan con tolerancia relativa de 1e-12. Textos, colores y tipos (`700` ≠ `700.0`) van exactos.
- **Compilación:** sin contracción a FMA (`-ffp-contract=off`), para que x86 y ARM den los mismos bits que Python.
- **Uso en F3:** es la mitad de `evaluate(snapshot, t)`; el render pinta con estos valores.
- **Rendimiento** (Release, timeline real de 167 clips, `move_clip` completo con *settle* y validación):

  | Motor | Tiempo por operación |
  |---|---|
  | C++ | **≈ 90 µs** |
  | video-yt | ≈ 49 ms |

  Un arrastre a 60 fps tiene 16 ms por fotograma: sobra margen.

## 5. EditSession: el documento vivo

```cpp
EditSession s(timeline);
s.apply("move_clip", {{"clip_id", "c1"}, {"start", 4.2}}); // o s.apply(ops::MoveClip{...})
{
    EditSession::Group drag(s);   // arrastrar = 60 move_clip por segundo…
    ...                           // …pero UN solo paso de deshacer
    drag.commit();                // sin commit: vuelve como estaba
}
s.undo(); s.redo(); s.checkpoint("antes de la IA"); s.restore_checkpoint("antes de la IA");
auto snap = s.timeline();         // shared_ptr<const Timeline>: se puede pasar a otro hilo
s.subscribe([](const TimelineSnapshot& tl, const Change& c) { /* UI, render, autosave */ });
```

- **Historial:** pilas de *snapshots* con un límite de 50, como `timeline_history.py`. Deshacer intercambia punteros: es O(1).
- **Revisión:** un contador que sube con cada cambio. Sirve de clave para las cachés: si la revisión no cambió, la miniatura o el fotograma cacheado siguen valiendo.
- **Observadores:** reciben el snapshot nuevo y un `Change` con la causa, los ids afectados y los avisos. La UI solo repinta lo que nombra `changed`.
- **Hilos:** una `EditSession` vive en el hilo de la UI. Entre hilos solo viajan snapshots inmutables.

## 6. Hilos *(F3–F5)*

| Hilo | Hace | Se comunica |
|---|---|---|
| UI | QML, EditSession, comandos | Publica snapshots |
| Render | `evaluate(snapshot, t)` + Skia sobre la GPU | Lee el snapshot más reciente y entrega fotogramas a la UI |
| Decodificación (pool) | FFmpeg con hardware, lectura anticipada por clip visible | Caché de fotogramas con presupuesto de RAM |
| Audio (tiempo real) | Mezcla y efectos; es el **reloj maestro** | Búferes circulares sin locks; nunca reserva memoria |
| Jobs (pool) | IA, transcripción, proxies, export | Progreso + resultado como comando |

## 7. Evaluación y render *(F3, diseño)*

- **`evaluate(const Timeline&, double t) → FrameDesc`:** función pura.
  - Calcula las capas visibles en orden de pila (`display_order`) y, para cada clip, el instante de fuente (velocidad, invertido, congelado).
  - Resuelve la pose y los keyframes, y los efectos con sus parámetros en *t*.
  - No dibuja nada.
- **El render consume un `FrameDesc`:** el preview lo pinta a ½ o ¼ de resolución y el export a 1:1, con el **mismo** código. Así se acaba la paridad a mano entre canvas.js y compose.py.
- **Cachés por contenido:** fotogramas decodificados, mattes de «quitar fondo» y maquetación de textos. Se invalidan con `changed` y la revisión, no repintando todo.

## 8. Persistencia

- **Proyectos:** lee y escribe `data/projects.json` de video-yt sin convertirlo. Con `Preserve` no se pierden las claves que solo conoce Python. La escritura es atómica: archivo temporal y reemplazo, con reintentos en Windows si otro proceso lo tiene abierto.
- **Pendiente:**
  - Autosave como observador de la sesión, con espera tras el último cambio (*debounce*).
  - Historial persistido por proyecto (`data/history/<id>.json`, como hoy).

## 9. Empaquetado: sí, un .exe

C++ produce ejecutables nativos. No hay que instalar ni Python, ni Node, ni un navegador.

| Pieza | Qué es |
|---|---|
| `GenClipPro.exe` | La app (Qt Quick). |
| `genclip.exe` | La herramienta de línea de órdenes (ya existe hoy). |
| DLL junto al .exe | Qt (`windeployqt` las copia), FFmpeg y ONNX Runtime. Skia y `gc_core` van enlazados dentro. |
| Instalador | CPack: **NSIS/WiX** (`GenClipPro-Setup.exe` / `.msi`) o **MSIX**. También un **ZIP portátil**. |
| Modelos de IA | No van en el instalador: se descargan la primera vez que se usan (RVM, BiRefNet, SAM, Kokoro, Whisper: 1–3 GB). |

**Tamaño aproximado del instalador sin modelos: 120–200 MB.** Las partes más pesadas son:
- runtime de Qt Quick: 40–70 MB;
- FFmpeg con sus códecs: 60–100 MB;
- ONNX Runtime CPU/DirectML: 20–40 MB.

CUDA y cuDNN (más de 1 GB) serían una descarga opcional para quien tenga NVIDIA.

**Requisitos para distribuirlo:**
1. **MSVC con el Windows SDK** (pendiente de instalar en esta máquina). La MinGW actual solo sirve para el núcleo.
2. **Firma de código:** sin certificado, SmartScreen avisará al instalar.
3. **Licencias** (plan §10): con x264 o espeak-ng el programa distribuido pasa a ser GPL.

En macOS sería un `.app` dentro de un `.dmg` firmado y notarizado; en Linux, AppImage o Flatpak; en Android e iOS, APK/AAB e IPA.

## 10. Cómo se verifica

| Prueba | Qué comprueba |
|---|---|
| Unitarias (`tests/unit`) | Coerciones, migraciones, `py_round`, sesión (deshacer, grupos, snapshots inmutables, estructura compartida). |
| Paridad (`tests/parity`) | Fixtures generadas por `tools/fixtures/export_fixtures.py` desde video-yt. Comparación estricta: tipos, valores bit a bit y orden de claves. |
| Pruebas de mutación | Alterar un valor esperado debe hacer fallar el test (comprobado en modelo y edición). |
| Rendimiento | `gc_tests "[.bench]"`. |
