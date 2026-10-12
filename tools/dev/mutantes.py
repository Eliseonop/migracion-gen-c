"""Pruebas de mutación: cada mutante de un grupo debe hacer fallar los tests de su etiqueta.

    python tools/dev/mutantes.py <grupo> [preset] [filtro]

`grupo`: eval (evaluación por clip), layers (capas de un instante), frame (fotograma), sfx
(biblioteca de SFX en disco), collections (colecciones de material), projects (almacén de
proyectos), json (JSON como Python), library («Guardar en la biblioteca»), stick (personajes
stick), motion (envolvente de un sonido, clip_motion) o media (cabeceras de imagen). Cada uno pasa los
tests de su etiqueta (GROUPS).
`preset`: la build ya compilada (por defecto mingw-release). `filtro`: solo los mutantes cuyo
nombre contiene ese texto.

Aplica cada cambio a la fuente, recompila gc_tests, pasa los tests del grupo y deshace el
cambio (también si algo falla). Un mutante «NO DETECTADO» es un hueco de las fixtures,
salvo que sea equivalente (da el mismo resultado): esos van anotados en la lista. Uno que
«cuelga» los tests (no acaban en TEST_TIMEOUT segundos) cuenta como detectado.
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
     "head < start + dur", "head <= start + dur"),
    ("fin con la duración de la fuente", "src/core/src/layers.cpp",
     "const double dur = clip_timeline_duration(*c);", "const double dur = clip_source_duration(*c);"),
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
     "rules.export_rules ? std::max(0.0, c->start) : c->start", "c->start"),
    ("preview: desempate por inicio", "src/core/src/layers.cpp",
     "if (rules.export_rules && a.start != b.start) {", "if (a.start != b.start) {"),
    ("render: sin mirar el encaje en la pista", "src/core/src/layers.cpp",
     "if (!fits || !*fits || dur <= kMinExportClipDur) {", "if (dur <= kMinExportClipDur) {"),
    ("render: dibuja clips de 0,02 s", "src/core/src/layers.cpp",
     "dur <= kMinExportClipDur", "dur < kMinExportClipDur"),
    ("render: dibuja clips cortos", "src/core/src/layers.cpp",
     "|| dur <= kMinExportClipDur", "|| dur <= 0"),
    ("preview: mira el encaje en la pista", "src/core/src/layers.cpp",
     "if (rules.export_rules) {\n            const auto fits", "if (true) {\n            const auto fits"),
    ("videos_at con textos", "src/core/src/layers.cpp", 'return c->kind == "text"; });', "return false; });"),
]

F = "src/core/src/frame.cpp"
FRAME = [
    ("overlay: sin el desplazamiento de la aparición", F, "dx + dw / 2 + fx.tx * dw", "dx + dw / 2"),
    ("overlay: sin volteo", F, "scaled(m, fx.scale * fh, fx.scale * fv)", "scaled(m, fx.scale, fx.scale)"),
    ("overlay: giro al revés", F, "m = rotated(m, rot * std::numbers::pi / 180);\n        m = scaled(m, fx.scale",
     "m = rotated(m, -rot * std::numbers::pi / 180);\n        m = scaled(m, fx.scale"),
    ("overlay: sin cortinilla", F, "if (fx.wipe < 1) {\n            layer.wipe = quad(m, dx, dy",
     "if (false) {\n            layer.wipe = quad(m, dx, dy"),
    ("relleno: sin la escala de la aparición", F, "    m = scaled(m, fx.scale, fx.scale);\n", ""),
    ("relleno: el render se salta la escala pequeña", F, "if (!preview || dx != 0", "if (dx != 0"),
    ("relleno: el preview aplica la escala pequeña", F, "if (!preview || dx != 0", "if (true || dx != 0"),
    ("relleno: la pose sin restar el centro", F, "(pose.x - 0.5) * out_w", "pose.x * out_w"),
    ("relleno: contain sin centrar", F, "area.dx + (area.dw - dw) / 2", "area.dx"),
    ("relleno: recorte de la pose ignorado", F, "        if (keyframes_enabled(clip)) {\n            const CropWindow",
     "        if (false) {\n            const CropWindow"),
    ("doble: zoom2 sin el zoom de respaldo", F, "rf.zoom2.value_or(rf.zoom)", "rf.zoom2.value_or(1.0)"),
    ("doble: keyframes2 sin respaldo", F, "rf.keyframes2.empty() ? rf.keyframes : rf.keyframes2", "rf.keyframes2"),
    ("doble: aspecto de la mitad", F, "out_aspect / 2 : out_aspect * 2", "out_aspect : out_aspect"),
    ("doble: orientación por defecto al revés", F, 'out_aspect < 1 ? "vertical" : "horizontal"',
     'out_aspect < 1 ? "horizontal" : "vertical"'),
    ("tiempo de fuente: al revés ignorado", F, "clip.reverse ? clip.out_point - local : clip.in_point + local",
     "clip.in_point + local"),
    ("tiempo de fuente: sin acotar", F, "return clamp(src, clip.in_point, clip.out_point);", "return src;"),
    ("opacidad sin la de la pose", F, "layer.fx.opacity * layer.pose.opacity", "layer.fx.opacity"),
    ("render: inicio sin acotar a 0", F, "preview ? c->start : std::max(0.0, c->start)", "c->start"),
    ("blit sin el origen del recorte", F, "d.dx - s.sx * d.dw / s.sw", "d.dx"),
    ("motion sin material", F, '|| clip.kind == "motion"', ""),
    ("render: material sin tamaño sin error", F, "                if (!preview) {", "                if (false) {"),
]

X = "src/core/src/sfx_disk.cpp"
D = "src/core/src/disk_scan.cpp"  # recorrido, resolve y carpeta de los ajustes, compartidos con las colecciones
SFX = [
    ("audio: sin .flac", X, ' || ext == ".flac"', ""),
    ("audio: sufijo sin minúsculas", X, "py_lower(py_path_suffix(filename))", "py_path_suffix(filename)"),
    ("escaneo: orden sin minúsculas", D, "f.key.push_back(py_lower(p));", "f.key.push_back(p);"),
    ("escaneo: sin ordenar", D, "std::ranges::stable_sort(found, {}, &Found::key);", ""),
    ("escaneo: carpeta con otro separador", X, '(k > 0 ? "/" : "")', '(k > 0 ? "|" : "")'),
    ("escaneo: sin subcarpetas", D, "if (all.size() < depth || ", "if (depth > 1 || "),
    ("índice: sin categorías", X, "categories.push_back(std::move(cat));", "(void)cat;"),
    ("índice: etiqueta sin defecto", X, 'get_or(v, "etiqueta", id)', 'get_or(v, "etiqueta", kNull)'),
    ("índice: cuenta por defecto 1", X, 'get_or(v, "cantidad", Json(0))', 'get_or(v, "cantidad", Json(1))'),
    ("índice: categoría que no es dict vale", X, "if (py_truthy(v) && !v.is_object()) {", "if (false) {"),
    ("índice: sonido que no es dict se salta", X, "return false; // s.get", "continue; // s.get"),
    ("índice: al fallar se borran las categorías", X, "        items = sfx_scan(*base);\n    }\n    Json out",
     "        categories.clear();\n        items = sfx_scan(*base);\n    }\n    Json out"),
    ("índice: con BOM se acepta", X, "read_json_file_py(file)", "read_json_file(file)"),
    ("índice: sin archivo no escanea", X, "if (!fs::exists(file, ec) || !read_index(",
     "if (fs::exists(file, ec) && !read_index("),
    ("entry: categoría sin la carpeta", X, "py_truthy(category) ? category : folder", "category"),
    ("entry: un nombre falso se queda", X, "if (py_truthy(name)) {", "if (!name.is_null()) {"),
    ("entry: carpeta falsa cuenta", X, "py_truthy(folder) ? Json(", "!folder.is_null() ? Json("),
    ("entry: URL sin codificar", X, "py_quote(rel.get_ref<const std::string&>())", "rel.get_ref<const std::string&>()"),
    ("base: sin sfx_folder", D, "if (it != settings.end() && it->is_string()) {", "if (false) {"),
    ("base: ruta sin normalizar", X, "py_path_str(path_to_utf8(*base))", "path_to_utf8(*base)"),
    ("filtro: categoría sin strip", X, "py_lower(py_strip(category))", "py_lower(category)"),
    ("filtro: categoría solo en la carpeta", X, 'py_lower(py_str(get_or(it, "category", kEmptyText)));',
     '"";'),
    ("filtro: alguna palabra basta", X, "std::ranges::all_of(tokens", "std::ranges::any_of(tokens"),
    ("filtro: palabras sin el uso", X, 'py_str(get_or(it, "uso", kEmptyText)) + " " + ', ""),
    # Equivalente: split() sin argumentos ya descarta los blancos de los extremos, y una búsqueda
    # solo de blancos da [] palabras: todo pasa, como con el strip.
    ("filtro: palabras sin strip (EQUIVALENTE)", X, "py_split(py_lower(py_strip(q)))", "py_split(py_lower(q))"),
    ("favoritos: se queda la categoría «favoritos»", X,
     "if (!(id.is_string() && id.get_ref<const std::string&>() == kSfxFavorites)) {", "if (true) {"),
    ("favoritos: cuenta todos", X, "[&](const Json& it) { return is_favorite(it, favorites); }",
     "[&](const Json&) { return true; }"),
    ("búsqueda: total tras el límite", X, 'out["total"] = total;', 'out["total"] = items.size();'),
    ("búsqueda: categorías sin favoritos", X,
     "with_favorites_category(idx.at(\"categories\").get<std::vector<Json>>(), all, favorites)",
     'idx.at("categories")'),
    ("resolve: sale de la carpeta", D,
     "if (t.size() < r.size() + (strict ? 1 : 0) || !std::equal(r.begin(), r.end(), t.begin())) {",
     "if (false) {"),
    ("resolve: distingue mayúsculas", D, "            out.push_back(py_lower(s));", "            out.push_back(s);"),
    ("resolve: sin comprobar que existe", X, "if (!target || !fs::exists(*target, ec)) {", "if (!target) {"),
    ("biblioteca: duración sin redondear", X, "py_round(*seconds, 3)", "*seconds"),
    ("biblioteca: sin duración da 1", X, "py_round(*seconds, 3) : 0.0;", "py_round(*seconds, 3) : 1.0;"),
    ("biblioteca: límite de 3", X, "constexpr std::size_t kAll = 100000;", "constexpr std::size_t kAll = 3;"),
]

C = "src/core/src/collections.cpp"
COLLECTIONS = [
    ("tipo: el GIF es imagen", C, 'if (ext == ".gif") {', "if (false) {"),
    ("tipo: sin .wma", C, '".wma"', '".wmx"'),
    ("tipo: sufijo sin minúsculas", C, "py_lower(py_path_suffix(filename))", "py_path_suffix(filename)"),
    ("raíz: sin material_root", C, '"material_root", default_dir', '"material_rot", default_dir'),
    ("set_root: sin strip", C, "py_path_str(py_strip(path))", "py_path_str(path)"),
    ("set_root: guarda aunque no sea carpeta", C, " || !fs::is_directory(path_from_utf8(text), ec)) {", ") {"),
    ("id: «..» vale", C, ' || name == ".."', ""),
    ("id: sin comparar con el texto", C, " || name != text", ""),
    ("prefs: etiqueta vacía se queda", C, 'cur.erase("label");', 'cur["label"] = "";'),
    ("prefs: etiqueta sin strip", C, "py_strip(*label)", "std::string(*label)"),
    ("prefs: no se guardan", C, 'settings.save(Json{{"collections_meta", meta}})', "Result<Json>(Json())"),
    ("entry: URL sin codificar", C, "py_quote(ref)", "ref"),
    ("entry: nombre con la extensión", C, "py_path_stem(rel)", "py_path_name(rel)"),
    ("entry: id sin prefijo", C, '"col_" + ref', "ref"),
    # Equivalente: collection_entry pasa la ruta por py_path_str y vuelve a poner «/».
    ("recorrido: otro separador (EQUIVALENTE)", C, '(rel.empty() ? "" : "/")', '(rel.empty() ? "" : "\\\\")'),
    ("lista: sin las favoritas primero", C,
     "std::tie(a.not_favorite, a.name_key) < std::tie(b.not_favorite, b.name_key)", "a.name_key < b.name_key"),
    ("lista: nombre sin minúsculas al ordenar", C, "py_lower(name.get_ref<const std::string&>())",
     "name.get_ref<const std::string&>()"),
    ("lista: carpetas en el orden del disco", D, "std::ranges::stable_sort(subs, {}, &Sub::key);", ""),
    ("lista: desactivadas por defecto", C, 'get_or(pref, "enabled", Json(true))', 'get_or(pref, "enabled", Json(false))'),
    ("lista: etiqueta que no es texto vale", C, "if (!name.is_string()) {", "if (false) {"),
    ("lista: portada solo de imágenes", C, 'kind == "image" || kind == "gif"', 'kind == "image"'),
    ("lista: sin stick", C, 'fs::is_regular_file(dir / "stick.json", ec_stick)', "false"),
    ("match: alguna palabra basta", C, "std::ranges::all_of(py_split(py_lower(q))", "std::ranges::any_of(py_split(py_lower(q))"),
    ("match: sin la colección", C, 'py_str(get_or(item, "collection", Json("")))', "std::string()"),
    ("búsqueda: cuenta tras filtrar por tipo", C,
     "counts[k] = counts.value(k, std::int64_t{0}) + 1;\n                if (!want_kind.empty() && k != want_kind) {\n"
     "                    continue;\n                }",
     "if (!want_kind.empty() && k != want_kind) {\n                    continue;\n                }\n"
     "                counts[k] = counts.value(k, std::int64_t{0}) + 1;"),
    ("búsqueda: total tras el límite", C, 'out["total"] = total;', 'out["total"] = items.size();'),
    ("búsqueda: límite negativo como 0", C, "std::max<std::int64_t>(0, n + limit)", "std::int64_t{0}"),
    ("búsqueda: tipo sin minúsculas", C, "py_lower(py_strip(kind))", "py_strip(kind)"),
    ("búsqueda: colección sin strip", C, "py_strip(collection)", "std::string(collection)"),
    ("búsqueda: también las desactivadas", C, 'if (c.at("enabled").get<bool>()) {', "if (true) {"),
    ("búsqueda: sin favorita", C, 'it["favorite"] = favorite.contains(cid);', 'it["favorite"] = false;'),
    ("crear: sufijo con guion bajo", C, 'std::format("{} {}", clean, n)', 'std::format("{}_{}", clean, n)'),
    ("nombre: los puntos de los extremos se quedan", D, 'out.find_first_not_of(" .")', 'out.find_first_not_of(" ")'),
    ("nombre: los dos puntos se quedan", D, r'R"(<>:"/\|?*)"', r'R"(<>"/\|?*)"'),
    ("nombre: los caracteres de control se quedan", D, "static_cast<unsigned char>(c) < 0x20 || ", ""),
    ("añadir: duplicado con espacio", C, 'std::format("{}_{}{}", stem, n, suffix)',
     'std::format("{} {}{}", stem, n, suffix)'),
    ("añadir: nombre con la ruta", C, "clean_name(py_path_name(py_truthy(f.name)", "clean_name((py_truthy(f.name)"),
    ("añadir: el error lleva el nombre limpio", C, '{"file", f.name}', '{"file", name}'),
    ("añadir: otro mensaje sin carpeta", C, '"Carpeta no encontrada."', '"Carpeta no encontrada"'),
    ("resolve: también carpetas", C, "!fs::is_regular_file(*target, ec)", "!fs::exists(*target, ec)"),
    ("añadir: un NUL en el id llega al disco", C, "cid->contains('\\0') || ", ""),
    ("recorrido: un NUL en el id llega al disco", C, "collection.contains('\\0') || ", ""),
]

P = "src/core/src/projects_store.cpp"
PROJECTS = [
    ("cargar: con BOM se acepta", P, "read_json_file_py(file_)", "read_json_file(file_)"),
    ("crear: nombre sin strip", P, "proj.name = py_strip(name);", "proj.name = std::string(name);"),
    ("crear: sin «Sin título»", P, 'proj.name = "Sin título";', 'proj.name = "";'),
    ("crear: al final de la lista", P, "(*projects)->insert((*projects)->begin(), to_json(proj));",
     "(*projects)->push_back(to_json(proj));"),
    ("crear: grupo inexistente se queda", P, "if (*exists) {\n            proj.group_id",
     "if (true) {\n            proj.group_id"),
    ("crear: id de 12", P, "proj.id = ids.hex32().substr(0, 8);", "proj.id = ids.hex32().substr(0, 12);"),
    ("grupo: nombre vacío vale", P, "if (group.name.empty()) {", "if (false) {"),
    ("renombrar grupo: no guarda", P, 'g["name"] = new_name;\n            if (auto saved = save(data); !saved) {',
     'g["name"] = new_name;\n            if (auto saved = Result<void>(); !saved) {'),
    ("borrar grupo: los proyectos se quedan en él", P, 'p["group_id"] = nullptr;', "(void)p;"),
    ("mover: carpeta inexistente vale", P, 'return fail(ErrorCode::InvalidArgument, "La carpeta no existe.");',
     "return std::optional<Project>();"),
    ("mover: «» no saca a la raíz", P, "const bool to_group = group_id && !group_id->empty();",
     "const bool to_group = group_id.has_value();"),
    ("renombrar: nombre vacío vale", P,
     "std::string_view name) {\n    const std::string new_name = py_strip(name);\n    if (new_name.empty()) {\n"
     "        return empty_name();\n    }\n    const std::scoped_lock lock(mutex_);\n    Json data = load();\n"
     "    auto p = find_project",
     "std::string_view name) {\n    const std::string new_name = py_strip(name);\n    if (false) {\n"
     "        return empty_name();\n    }\n    const std::scoped_lock lock(mutex_);\n    Json data = load();\n"
     "    auto p = find_project"),
    ("duplicar: copia exports", P, "copy_tree(src_base, dest_base, true)", "copy_tree(src_base, dest_base, false)"),
    ("duplicar: «Exports» se copia", P, 'py_lower(path_to_utf8(it->path().filename())) == "exports"',
     'path_to_utf8(it->path().filename()) == "exports"'),
    ("duplicar: sin la carpeta motion", P, 'if (fs::exists(motion, ec) && !fs::exists(dest_base / "motion", ec)) {',
     "if (false) {"),
    ("duplicar: sin cambiar las URLs", P, 'for (const std::string_view prefix : {"/api/media/", "/api/projects/"}) {',
     'for (const std::string_view prefix : {"/api/nada/"}) {'),
    ("duplicar: solo /api/media/", P, 'for (const std::string_view prefix : {"/api/media/", "/api/projects/"}) {',
     'for (const std::string_view prefix : {"/api/media/"}) {'),
    ("duplicar: al final", P, "list.insert(list.begin() + at, copy);", "list.push_back(copy);"),
    ("duplicar: sin «(copia)»", P, '(*src)->name + " (copia)"', "(*src)->name"),
    ("duplicar: conserva la carpeta propia", P, 'copy["folder"] = nullptr;', ""),
    ("duplicar: sin subcarpetas", P, "if (auto made = ensure_project_dirs(dest_base); !made) {",
     "if (auto made = Result<void>(); !made) {"),
    ("set_folder: con migración", P,
     '(**p)["folder"] = path;\n    if (auto saved = save(data); !saved) {\n        return std::unexpected(saved.error());\n'
     "    }\n    auto project = project_from_json_as_is(**p);",
     '(**p)["folder"] = path;\n    if (auto saved = save(data); !saved) {\n        return std::unexpected(saved.error());\n'
     "    }\n    auto project = project_from_json(**p);"),
    ("timeline: con migración", P,
     '(**p)["timeline"] = timeline;\n    if (auto saved = save(data); !saved) {\n        return std::unexpected(saved.error());\n'
     "    }\n    auto project = project_from_json_as_is(**p);",
     '(**p)["timeline"] = timeline;\n    if (auto saved = save(data); !saved) {\n        return std::unexpected(saved.error());\n'
     "    }\n    auto project = project_from_json(**p);"),
    ("borrar: no reescribe si no está", P, "    if (auto saved = save(data); !saved) {\n        return std::unexpected(saved.error());\n"
     "    }\n    return after < before;",
     "    if (auto saved = after < before ? save(data) : Result<void>(); !saved) {\n"
     "        return std::unexpected(saved.error());\n    }\n    return after < before;"),
    ("clips: sin upsert por index", P, 'if (py_equal(**index, d["index"])) {', "if (false) {"),
    ("clips: pierde el id del anterior", P, 'd["id"] = prev_id;', "(void)prev_id;"),
    ("clips: id sin el index", P, 'd["id"] = py_truthy(index) ? py_str(index) : ids.hex32().substr(0, 12);',
     'd["id"] = ids.hex32().substr(0, 12);'),
    ("clips: sin fecha", P, 'd["created_at"] = stamp;', ""),
    ("material: el id antes que el index", P,
     'const Json& key = index != item.end() ? *index : item.value("id", Json());',
     'const Json& key = item.value("id", Json());'),
    ("material: null también se aplica", P, "if (!it->is_null()) {\n                    item[it.key()] = *it;",
     "if (true) {\n                    item[it.key()] = *it;"),
    ("manifest: audio sin voz", P, '{"audios", {"label", "description", "voice"}}', '{"audios", {"label", "description"}}'),
    ("manifest: gana el primero", P, "if (*found != nullptr) {", "if (*found != nullptr && false) {"),
    ("manifest: nombre sin strip", P, "new_name = py_strip((*name)->get_ref<const std::string&>());",
     "new_name = (*name)->get_ref<const std::string&>();"),
    ("motion: sin id también", P, "if (!py_truthy(**cid)) {", "if (false) {"),
    ("motion: siempre añade", P, "if (py_equal(**id, **cid)) {\n            c = comp;", "if (false) {\n            c = comp;"),
    ("retarget: sin mirar el tipo", P, "if (!py_equal(**c_kind, kind)) {", "if (false) {"),
    ("retarget: por nombre siempre", P, "(match_filename && py_equal(", "(py_equal("),
    ("retarget: sin asset_scope", P, 'c["asset_scope"] = "library";', ""),
    ("forma: un dict vacío falla", P, "if ((seq.is_object() && seq.empty()) ||", "if ((false) ||"),
    ("forma: un proyecto sin id se salta", P, 'return shape_error("un elemento no tiene «id»");', "return false;"),
    ("py_equal: True no es 1", "src/core/src/py.cpp", "out = j.get<bool>() ? 1 : 0;", "out = j.get<bool>() ? 7 : 0;"),
]

IO = "src/core/src/io.cpp"
PY = "src/core/src/py.cpp"
JSON = [
    ("repr: científica desde 1e15", PY, "if (decpt <= -4 || decpt > 16) {", "if (decpt <= -4 || decpt > 15) {"),
    ("repr: científica desde 1e-04", PY, "if (decpt <= -4 || decpt > 16) {", "if (decpt <= -3 || decpt > 16) {"),
    ("repr: exponente sin el 0", PY, 'out += (a < 10 ? "0" : "") + std::to_string(a);', "out += std::to_string(a);"),
    ("repr: entero sin .0", PY, 'out.append(static_cast<std::size_t>(decpt - n), \'0\');\n        out += ".0";',
     "out.append(static_cast<std::size_t>(decpt - n), '0');"),
    ("repr: -0.0 como 0.0", PY, 'return std::signbit(x) ? "-0.0" : "0.0";', 'return "0.0";'),
    ("dumps: sin espacio tras la coma", IO, 'out += indent < 0 ? ", " : ",";', 'out += ",";'),
    ("dumps: espacio tras la coma con sangría", IO, 'out += indent < 0 ? ", " : ",";', 'out += ", ";'),
    ("dumps: \\u00XX en mayúsculas", IO, r'std::format("\\u{:04x}"', r'std::format("\\u{:04X}"'),
    ("dumps: DEL escapado", IO, "if (static_cast<unsigned char>(ch) < 0x20) {",
     "if (static_cast<unsigned char>(ch) < 0x20 || ch == 0x7f) {"),
    ("dumps: NaN como null", IO, 'out += std::isnan(d) ? "NaN"', 'out += std::isnan(d) ? "null"'),
    ("escribir: sin CRLF en Windows", IO, "        if (c == '\\n') {\n            crlf += '\\r';\n        }\n", ""),
    ("leer: Infinity no se marca", IO, 'rest.starts_with("Infinity")', 'rest.starts_with("Infinityy")'),
    ("leer: sin desbordes", IO, "if (overflows(token)) {", "if (false) {"),
    ("leer: el desborde negativo es +inf", IO, "token.starts_with('-') ? m.neg_inf : m.inf", "m.inf"),
    ("leer: los textos se cierran en cualquier carácter", IO, "in_string = c != '\"';", "in_string = false;"),
    ("leer: sin saltar los escapes", IO, "i += v >= 0 ? 6 : 2;", "i += 1;"),
    ("leer: lo que se sale por abajo también desborda", IO,
     "return static_cast<long long>(int_part.size() - first) - 1 + exp10 > 0;", "return true;"),
    ("leer: NaN como clave se acepta", IO, "if (!is_key(at + len)) {", "if (true) {"),
    ("leer: un texto igual al marcador pasa a NaN", IO, "if (restore_marks(j, marks) <= marked->marks) {", "if (true) {"),
    ("leer: sustituto alto suelto sin cambiar", IO,
     "                    } else {\n                        put(i, 6, " + r'"\\ufffd"' + ");",
     "                    } else {\n                        (void)0;"),
    ("leer: sustituto bajo suelto sin cambiar", IO,
     "v <= 0xDFFF) {\n                    put(i, 6, " + r'"\\ufffd"' + ");", "v <= 0xDFFF) {\n                    (void)0;"),
    ("escribir: sin límite de profundidad", IO, "if (level > kMaxDumpDepth) {", "if (false) {"),
    ("duplicar: los NaN pasan a null", "src/core/src/projects_store.cpp", "auto parsed = py_json_loads(text);",
     'auto parsed = py_json_loads(replace_all(text, "NaN", "null"));'),
]

L = "src/core/src/library.cpp"
LIBRARY = [
    ("guardar: tipo raro vale", L, 'if (resource_type != "audio" && resource_type != "clip" && resource_type != "image") {',
     "if (false) {"),
    ("guardar: clip por id", L, "std::to_string(c.index) == ident; });", "(c.id ? *c.id : std::string()) == ident; });"),
    ("guardar: sin mirar que existe", L, "if (!src || !std::filesystem::exists(*src, ec)) {", "if (!src) {"),
    ("guardar: id de 8", L, '"lib_" + ids.hex32().substr(0, 12)', '"lib_" + ids.hex32().substr(0, 8)'),
    ("guardar: audio sin extensión es .mp3", L, 'is_audio ? ".m4a"', 'is_audio ? ".mp3"'),
    ("guardar: nombre sin la etiqueta", L, "py_truthy(label) ? py_str(label) : detail::py_path_stem(filename)",
     "detail::py_path_stem(filename)"),
    ("guardar: solo la carpeta audio", L, 'for (const char* sub : {"audio", "video", "image"}) {',
     'for (const char* sub : {"audio"}) {'),
    ("guardar: duración sin start/end", L,
     'if (duration.is_null() && !get("start").is_null() && !get("end").is_null()) {', "if (false) {"),
    ("guardar: descripción sin el texto", L, 'py_truthy(description) ? description : get("text")', "description"),
    ("guardar: youtube_url sin source_url", L, 'origin == "youtube" ? get("source_url") : Json()', "Json()"),
    ("guardar: compartido también se borra", L, "if (!shared) {\n        std::filesystem::remove(*src, ec);",
     "if (true) {\n        std::filesystem::remove(*src, ec);"),
    ("guardar: compartido empareja por nombre", L, "Json(lib_id), Json(dest_name), !shared);",
     "Json(lib_id), Json(dest_name), true);"),
    ("guardar: el propio cuenta como compartido", L, "c.filename == filename && std::to_string(c.index) != exclude_ident &&",
     "c.filename == filename &&"),
    ("guardar: sin deshacer el alta", L, "        items = std::move(kept);\n        if (!save(data)) {",
     "        (void)kept;\n        if (false) {"),
    ("guardar: sin borrar la copia al deshacer", L, "        std::filesystem::remove(dest, ec_rm);", ""),
    ("guardar: items {} no se guarda al deshacer", L, "} else if (!(items.is_object() && items.empty()) &&",
     "} else if (true &&"),
    ("guardar: sin quitarlo del proyecto", L, "auto removed = projects.remove_material(project_id, asset_kind, ident);",
     "gc::Result<std::optional<Json>> removed = std::optional<Json>(Json());"),
    ("safe_name: sin juntar espacios", "src/core/src/projects_store.cpp",
     "for (const auto& word : py_split(py_strip(kept))) {",
     "for (const auto& word : std::vector<std::string>{py_strip(kept)}) {"),
    ("safe_name: 59 caracteres", "src/core/include/gc/core/projects_store.hpp", "std::size_t maxlen = 60", "std::size_t maxlen = 59"),
    ("safe_name: sin «video»", "src/core/src/projects_store.cpp", 'return out.empty() ? "video" : out;', "return out;"),
    ("resolve_media: la propia carpeta vale", "src/core/src/projects_store.cpp",
     "return detail::resolve_inside(project_base(clips_dir, project), {kind, filename}, true);",
     "return detail::resolve_inside(project_base(clips_dir, project), {kind, filename}, false);"),
    ("resolve_media: unir como texto", "src/core/src/projects_store.cpp",
     "return detail::resolve_inside(project_base(clips_dir, project), {kind, filename}, true);",
     'return detail::resolve_inside(project_base(clips_dir, project), {std::string(kind) + "/" + std::string(filename)}, true);'),
    ("resolve_media: NUL vale", "src/core/src/projects_store.cpp",
     "if (numbered || std::ranges::find(kDevices, device) != kDevices.end()) {", "if (false) {"),
    ("pathlib: una raíz sin unidad se añade", "src/core/src/disk_scan.cpp", "return base.root_name() / p;",
     "return base / p.relative_path();"),
    ("pathlib: la misma unidad sustituye", "src/core/src/disk_scan.cpp",
     "if (!same_drive || p.has_root_directory()) {", "if (true) {"),
]

S = "src/core/src/stick_library.cpp"
STICK = [
    ("norm: sin quitar «_»", S, "std::erase(s, '_');", ""),
    ("slug: sin juntar rachas", S, "if (c != '_' || s.empty() || s.back() != '_') {", "if (true) {"),
    ("slug: 41 caracteres", S, "std::min<std::size_t>(s.size(), 40)", "std::min<std::size_t>(s.size(), 41)"),
    ("slug: sin el segundo strip", S, "    strip(s);\n    return s.empty()", "    return s.empty()"),
    ("pretty: \\d solo ASCII", S, "detail::py_isdecimal(chars[j].first)",
     "(chars[j].first >= U'0' && chars[j].first <= U'9')"),
    ("pretty: $ sin el \\n final", S, " || (k + 1 == n && chars[k].first == U'\\n')", ""),
    ("pretty: 5 cifras con p", S, "(digits == 3 || digits == 4)", "(digits >= 3 && digits <= 5)"),
    ("pretty: 7 cifras bastan", S, "digits >= 8 && boundary(j)", "digits >= 7 && boundary(j)"),
    ("pretty: sin strip", S, "    s = py_strip(s);\n", ""),
    ("manifiesto: con BOM vale", S, "read_json_file_py(file)", "read_json_file(file)"),
    ("defs: sin las de fábrica", S, "if (defs.empty()) {", "if (false) {"),
    ("defs: sin id también", S, 'e.is_object() && py_truthy(get(e, "id"))', "e.is_object()"),
    ("carpetas: sin la etiqueta", S, 'stick_norm(or_else(get(e, "label"), Json("")))', "std::string()"),
    ("carpeta propia: id sin slug", S, "Json id = stick_slug(Json(name), kOther);", "Json id = name;"),
    ("carpeta propia: sin emoji", S, 'Json("📁")', 'Json("")'),
    ("expresiones: repetidas", S, "if (!key_index(seen, id)) {", "if (true) {"),
    ("expresiones: sin las propias", S, "if (f.custom && !key_index(seen, f.id)) {", "if (false) {"),
    ("claves: igualdad de nlohmann", S, "    return py_equal(a, b);\n}", "    return a == b;\n}"),
    ("items: la repetida va al final", S,
     "if (found != listed.end()) {\n                found->second = std::move(it);\n            } else {",
     "if (false) {\n            } else {"),
    ("items: sin mirar que existe", S, 'if (!collection_resolve(base, std::format("{}/{}", cid, rel))) {',
     "if (false) {"),
    ("items: sin «\\» → «/»", S, 'std::string key = slashes(py_str(it.at("file")));',
     'std::string key = py_str(it.at("file"));'),
    ("tarjeta: GIF sin animado", S, 'card["animated"] = true;', ""),
    ("tarjeta: rel sin normalizar", S, 'card["rel"] = slashes(py_path_str(rel));', 'card["rel"] = rel;'),
    ("tarjeta: sin GIF", S, 'collection_kind == "image" || collection_kind == "gif"', 'collection_kind == "image"'),
    ("escaneo: repite los del manifiesto", S, "!= listed.end()) {\n                continue;",
     "!= listed.end() && false) {\n                continue;"),
    ("carpeta: solo con 3 partes", S, "if (parts.size() < 2) {", "if (parts.size() < 3) {"),
    ("lista: nombre sin minúsculas", S, "l.name_key = py_lower(name.get_ref<const std::string&>());",
     "l.name_key = name.get_ref<const std::string&>();"),
    ("lista: portada también vídeo", S, 'return c.at("stick_kind") == "image"; });', "return true; });"),
    ("lista: sin orden", S, "std::tie(a.order, a.name_key) < std::tie(b.order, b.name_key)", "false"),
    ("lista: order por defecto 0", S, "constexpr std::int64_t kNoOrder = 999;", "constexpr std::int64_t kNoOrder = 0;"),
    ("detalle: sin orden de expresiones", S, "    all = sort_ids(all, ordered);\n", ""),
    ("detalle: etiqueta sin title()", S, 'out["label"] = detail::py_title(text);', 'out["label"] = text;'),
    ("detalle: grupos con ==", S, 'if (py_equal(c.at("expression"), eid)) {', 'if (c.at("expression") == eid) {'),
    ("crear: sin subcarpetas", S, "fs::create_directory(folder / path_from_utf8(stick_slug(Json(e.label), e.id)), ec);",
     "(void)e;"),
    ("crear: order desde 1", S, 'man["order"] = top ? *top + 1 : 0;', 'man["order"] = top ? *top + 1 : 1;'),
    ("crear: order cuenta los 999", S, "order < kNoOrder) {", "order <= kNoOrder) {"),
    ("crear: emoji de 5", S, "py_strip(str_or_empty(emoji)), 4)", "py_strip(str_or_empty(emoji)), 5)"),
    ("crear: color sin strip", S, "std::string color = py_strip(str_or_empty(chroma_color));",
     "std::string color = str_or_empty(chroma_color);"),
    ("crear: hex de 7 o más", S, "color.size() == 7 &&", "color.size() >= 7 &&"),
    ("crear: sufijo con espacio", S, 'cid = std::format("{}_{}", stem, n);', 'cid = std::format("{} {}", stem, n);'),
    ("crear: nombre sin strip", S, "const std::string clean = py_strip(str_or_empty(name));",
     "const std::string clean = str_or_empty(name);"),
    # Equivalente: «archivo» no tiene extensión, así que tampoco es un vídeo ni una imagen.
    ("añadir: sin «archivo» (EQUIVALENTE)", S, 'name = "archivo";', ""),
    ("añadir: el audio vale", S, "if (!kind || !stick_kind(*kind)) {", "if (!kind) {"),
    ("añadir: expresión por defecto vacía", S, ": std::string(kOther);\n        require_expression",
     ": std::string();\n        require_expression"),
    ("añadir: carpeta sin la etiqueta", S, 'or_else(def == defs.end() ? kNull : get(*def, "label"), want)', "want"),
    ("añadir: una carpeta que es un archivo vale", S,
     "fs::create_directory(target, ec); // mkdir(exist_ok=True): falla si es un archivo\n    if (ec) {",
     "fs::create_directory(target, ec); // mkdir(exist_ok=True): falla si es un archivo\n    if (false) {"),
    ("set: sin lstrip", S, "rel.erase(0, std::min(rel.find_first_not_of('/'), rel.size())); // .lstrip(\"/\")", ""),
    ("set: fuera del stick vale", S, " || !detail::resolved_inside(s.base / path_from_utf8(s.cid), *path, true)", ""),
    ("set: título sin la tarjeta", S, 'card != cards.end() ? card->at("title") : ', 'false ? card->at("title") : '),
    ("set: los items raros se quedan", S, "if (it.is_object()) {\n                items.push_back",
     "if (true) {\n                items.push_back"),
    ("set: la propia no pasa a expressions", S, "if (!key_index(ids_of(defs), Json(expr))) {", "if (false) {"),
    ("set: siempre la añade", S, "if (!key_index(ids_of(defs), Json(expr))) {", "if (true) {"),
    ("set: busca por la ruta normalizada", S, 'return slashes(str_or_empty(get(it, "file"))) == rel;',
     'return py_path_str(str_or_empty(get(it, "file"))) == py_path_str(rel);'),
    ("title: sin la sigma final", PY, "if (c == kCapitalSigma) {", "if (false) {"),
    ("title: la caja del anterior por espacios", PY, "previous_is_cased = in_ranges(kTitleCased, c);",
     "previous_is_cased = c != U' ';"),
    ("title: sin tabla de título", PY, "} else if (const char* t = lookup(kTitle, c)) {",
     "} else if (const char* t = nullptr) {"),
    ("fold: ASCII sin minúsculas", PY, "const char a = ascii_lower(static_cast<char>(c));",
     "const char a = static_cast<char>(c);"),
    ("fold: sin tabla", PY, "} else if (const char* f = lookup(kFold, c)) {", "} else if (const char* f = nullptr) {"),
]

M = "src/core/src/clip_motion.cpp"
MOTION = [
    ("suma ingenua: sin la compensación final", M, "if (c != 0.0 && std::isfinite(c)) {", "if (false) {"),
    ("suma: compensación siempre por la primera rama", M, "if (std::fabs(f) >= std::fabs(x)) {", "if (true) {"),
    ("suma: compensación aunque no sea finita", M, "if (c != 0.0 && std::isfinite(c)) {", "if (c != 0.0) {"),
    ("downsample: round() lejos del 0", M, "std::nearbyint(q)", "std::round(q)"),
    ("downsample: max_n negativo devuelve los puntos", M, "return Envelope{}; // range(max_n) vacío", "return points;"),
    ("downsample: max_n = 1 no falla", M, "if (max_n == 1) {", "if (max_n == 1 && n == 0) {"),
    ("normalize: un pico negativo vale 1", M, "if (peak == 0.0) {", "if (peak <= 0.0) {"),
    ("normalize: un NaN gana el máximo", M, "if (a > peak) {", "if (!(a <= peak)) {"),
    ("normalize: suelo de 0,05", M, "(a / peak - 0.06) / 0.94", "(a / peak - 0.05) / 0.94"),
    ("normalize: sin tope de 1", M, "out.emplace_back(t, std::min(1.0, u));", "out.emplace_back(t, u);"),
    ("solape: 0,02 s justos cuentan", M, "if (hi - lo <= 0.02) {", "if (hi - lo < 0.02) {"),
    ("solape: fin con el máximo", M, "const double hi = std::min(ve, ae);", "const double hi = std::max(ve, ae);"),
    ("audio_src_at: antes del inicio, negativo", M, "std::max(0.0, t_tl - start)", "(t_tl - start)"),
    ("audio_src_at: speed sin clip_speed", M, "* clip_speed(audio);", "* audio.speed;"),
    ("audio_src_at: sin in_point", M, "return inp + std::max", "return 0.0 * inp + std::max"),
    ("rms: rate mínimo 2", M, "static_cast<double>(std::max(1, rate))", "static_cast<double>(std::max(2, rate))"),
    ("rms: una muestra basta", M, "if (n < 2) {", "if (n < 1) {"),
    ("rms: cola con 0,03 s", M, "if (pts.back().first < dur - 0.02) {", "if (pts.back().first < dur - 0.03) {"),
    ("rms: duración mínima 0,04", M, "std::max(0.05, num_or(duration, 0.0))", "std::max(0.04, num_or(duration, 0.0))"),
    ("rms: sin normalizar", M, "return *downsample_envelope(normalize_amps(pts));", "return *downsample_envelope(pts);"),
    ("rms: media del trozo entre hop", M, "/ static_cast<double>(last - i));", "/ static_cast<double>(hop));"),
    ("rms: t sin redondear", M, "pts.emplace_back(py_round(static_cast<double>(i) / kRmsSampleRate, 3), rms);",
     "pts.emplace_back(static_cast<double>(i) / kRmsSampleRate, rms);"),
]

G = "src/core/src/image_probe.cpp"
MEDIA = [
    ("gif: retardo 0 vale 0", G, "static_cast<double>(delay_cs) / 100.0 : 0.1;", "static_cast<double>(delay_cs) / 100.0 : 0.0;"),
    ("gif: GCE con un byte menos", G, "if (label == 0xF9 && pos + 6 < n) {", "if (label == 0xF9 && pos + 5 < n) {"),
    ("gif: transparencia con otro bit", G, "(data[pos + 3] & 0x01U)", "(data[pos + 3] & 0x02U)"),
    ("gif: NETSCAPE con contador ≠ 0 repite", G, "loop = le(data, sb + 2, sb + 4) == 0;", "loop = le(data, sb + 2, sb + 4) != 0;"),
    ("gif: sin NETSCAPE, un fotograma repite", G, "out.loop = netscape ? loop : frames > 1;", "out.loop = netscape ? loop : frames > 0;"),
    ("gif: sin saltar la tabla local", G, "pos += color_table(img_packed);", "pos += 0;"),
    ("gif: descriptor cortado un byte antes", G, "if (pos + 9 >= n) {", "if (pos + 10 >= n) {"),
    ("gif: el retardo no se reinicia", G, "total_delay += pending_delay;\n            pending_delay = 0.1;",
     "total_delay += pending_delay;"),
    ("gif: sub-bloques terminan en 1", G, "if (size == 0) {", "if (size == 1) {"),
    ("gif: GIF87a no vale", G, 'if (data.size() < 13 || !(slice_is(data, 0, "GIF87a") || slice_is(data, 0, "GIF89a"))) {',
     'if (data.size() < 13 || !slice_is(data, 0, "GIF89a")) {'),
    ("gif: fps sin redondear", G, "py_round(static_cast<double>(frames) / total_delay, 3)",
     "static_cast<double>(frames) / total_delay"),
    ("gif: tabla de 2^(bits)", G, "return std::size_t{3} << ((packed & 0x07U) + 1U);", "return std::size_t{3} << (packed & 0x07U);"),
    ("firma: WEBP de 13 bytes", G, "data.size() >= 12 &&", "data.size() >= 13 &&"),
    ("firma: JPEG de 2 bytes", G, 'slice_is(data, 0, "\\xff\\xd8\\xff")', 'slice_is(data, 0, "\\xff\\xd8")'),
]

X2 = "src/core/src/export_input.cpp"
EXPORT_INPUT = [
    ("still: images no cuenta", X2, ' || clip.asset_kind == "images"', ""),
    ("ventana: still sin el mínimo de 0,1", X2, "return {0.0, std::max(out - inp, 0.1)};", "return {0.0, out - inp};"),
    ("ventana: margen del final de 2e-4", X2, "out >= src - 1e-4", "out >= src - 2e-4"),
    ("ventana: sin el fotograma como mínimo", X2, "const double pull = std::max(kLastFramePull, frame);",
     "const double pull = kLastFramePull;"),
    ("ventana: fps 0 vale 1", X2, "fps != 0 ? fps : 30", "fps != 0 ? fps : 1"),
    ("ventana: sin el mínimo de entrada + 1 ms", X2, "out = std::max(inp + 1e-3, src - pull);", "out = src - pull;"),
    ("filtro: nan con signo", X2, 'std::isnan(x) ? std::string("nan") : ', ""),
    ("entrada: webm sin minúsculas", X2, 'ends_with(detail::py_lower(p), ".webm")', 'ends_with(p, ".webm")'),
    ("entrada: gif con loop None = 1", X2, 'clip.loop == false ? "1" : "0"', '!clip.loop.value_or(false) ? "1" : "0"'),
    ("entrada: still sin los 0,05 s", X2, "ffmpeg_trim_window(clip).second + 0.05", "ffmpeg_trim_window(clip).second"),
    ("entrada: secuencia sin «%0»", X2, 'if (p.contains("%0")) {', 'if (p.contains("%")) {'),
]

AU = "src/core/src/audio_export.cpp"
AUDIO = [
    ("envolvente: span mínimo de 1e-5", AU, "1e-6 > d ? 1e-6 : d", "1e-5 > d ? 1e-5 : d"),
    ("envolvente: tope de 119 tramos", AU, "std::min(120.0, std::max(2.0, steps))", "std::min(119.0, std::max(2.0, steps))"),
    ("envolvente: constante sin redondear a 4", AU, "values.insert(py_round(p.v, 4) + 0.0);", "values.insert(p.v + 0.0);"),
    ("envolvente: efecto sin max(0)", AU, "return n > 0.0 ? n : 0.0; // max(0.0, n) de Python", "return n;"),
    ("envolvente: hold vale 1", AU, 'case Interp::Hold: return "0";', 'case Interp::Hold: return "1";'),
    ("intensidad: el texto no cuenta", AU, "const auto n = py_float(value);",
     "const auto n = value.is_number() ? py_float(value) : std::nullopt;"),
    ("etapas: umbral de 1e-3", AU, "weights[static_cast<std::size_t>(j)] > 1e-4", "weights[static_cast<std::size_t>(j)] > 1e-3"),
    ("compresor: compensación ^−0,5", AU, "std::pow(full, -0.6)", "std::pow(full, -0.5)"),
    ("atempo: sin el margen de 1e-9", AU, "while (s > 2.0 + 1e-9) {", "while (s > 2.0) {"),
    ("adelay también en 0", AU, "if (start_ms > 0) {", "if (start_ms >= 0) {"),
    ("loudnorm: tope de −4", AU, "target_db < -5.0 ? target_db : -5.0", "target_db < -4.0 ? target_db : -4.0"),
    ("pistas: manda la primera", AU, "track_by_id[t.id] = &t;", "track_by_id.try_emplace(t.id, &t);"),
    ("los fijos también suenan", AU, "if (!is_still_clip(c) && clip_mixes_audio(c, track) && has_audio(c)) {",
     "if (clip_mixes_audio(c, track) && has_audio(c)) {"),
]
CF = "src/core/src/clip_filters.cpp"
FILTERS = [
    ("pila: orden al revés", CF, "m = m ? compose_color(fm, *m) : fm;", "m = m ? compose_color(*m, fm) : fm;"),
    ("pila: umbral de 1e-3", CF, "if (!(f.amount > 1e-4)) {", "if (!(f.amount > 1e-3)) {"),
    ("intensidad que falta vale 0,5", CF, "amount == f.end() ? 1.0 : clamp01(*amount)", "amount == f.end() ? 0.5 : clamp01(*amount)"),
    ("una lista vacía no manda", CF, "if (clip.filters && clip.filters->is_array()) {",
     "if (clip.filters && clip.filters->is_array() && !clip.filters->empty()) {"),
    ("ajuste: NaN vale 0", CF, "const double above = n > lo ? n : lo;", "const double above = std::isnan(n) ? 0.0 : (n > lo ? n : lo);"),
    ("ffmpeg: desplazamiento desde 1e-4", CF, "std::abs(row[3]) > 1e-5", "std::abs(row[3]) > 1e-4"),
    ("ajuste: sin la intensidad", CF, "const double k = clip.opacity ? clamp01(*clip.opacity) : 1.0;", "const double k = 1.0;"),
]

GROUPS = {"eval": (EVAL, "[eval]"), "layers": (LAYERS, "[layers]"), "frame": (FRAME, "[frame]"),
          "export_input": (EXPORT_INPUT, "[export_input]"), "audio": (AUDIO, "[audio]"), "filters": (FILTERS, "[eval]"),
          "stick": (STICK, "[stick]"), "motion": (MOTION, "[motion]"), "media": (MEDIA, "[media]"),
          "library": (LIBRARY, "[library],[projects]"),
          "sfx": (SFX, "[sfx]"), "collections": (COLLECTIONS, "[collections]"), "projects": (PROJECTS, "[projects]"),
          "json": (JSON, "[json],[projects],[settings],[sfx]")}


def run(cmd: list[str], timeout: float | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, cwd=REPO, env=ENV, capture_output=True, text=True, encoding="utf-8",
                          errors="replace", timeout=timeout)


# Un mutante puede colgar los tests (un bucle que no acaba): pasado este tiempo se mata el
# proceso y cuenta como detectado.
TEST_TIMEOUT = 120


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
    missing = 0
    for name, rel, old, new in mutants:
        path = REPO / rel
        original = path.read_text(encoding="utf-8")
        if old not in original:
            print(f"??            {name}: no encuentro el texto")
            missing += 1
            continue
        path.write_text(original.replace(old, new, 1), encoding="utf-8", newline="\n")
        try:
            b = run([cmake, "--build", str(build), "--target", "gc_tests"])
            if b.returncode != 0:
                print(f"NO COMPILA    {name}\n{b.stdout[-800:]}")
                continue
            try:
                ok = run([str(tests), tag], timeout=TEST_TIMEOUT).returncode != 0
                how = "detectado     "
            except subprocess.TimeoutExpired:
                ok, how = True, "cuelga        "
            caught += ok
            print((how if ok else "NO DETECTADO  ") + name, flush=True)
        finally:
            path.write_text(original, encoding="utf-8", newline="\n")
    run([cmake, "--build", str(build), "--target", "gc_tests"])
    print(f"{caught}/{len(mutants)} detectados")
    if missing:
        print(f"{missing} mutantes no encuentran su texto: actualízalos")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
