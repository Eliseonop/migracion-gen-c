"""Pruebas de mutación: cada mutante de un grupo debe hacer fallar los tests de su etiqueta.

    python tools/dev/mutantes.py <grupo> [preset] [filtro]

`grupo`: eval (evaluación por clip, tests [eval]), layers (capas de un instante, tests
[layers]), frame (fotograma, tests [frame]), sfx (biblioteca de SFX en disco, tests [sfx]) o
collections (colecciones de material, tests [collections]).
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
    ("resolve: sale de la carpeta", D, "if (t.size() < r.size() || !std::equal(r.begin(), r.end(), t.begin())) {",
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
    ("lista: carpetas en el orden del disco", C, "std::ranges::stable_sort(subs, {}, &Sub::key);", ""),
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
    ("nombre: los puntos de los extremos se quedan", C, 'out.find_first_not_of(" .")', 'out.find_first_not_of(" ")'),
    ("nombre: los dos puntos se quedan", C, r'R"(<>:"/\|?*)"', r'R"(<>"/\|?*)"'),
    ("nombre: los caracteres de control se quedan", C, "static_cast<unsigned char>(c) < 0x20 || ", ""),
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

GROUPS = {"eval": (EVAL, "[eval]"), "layers": (LAYERS, "[layers]"), "frame": (FRAME, "[frame]"),
          "sfx": (SFX, "[sfx]"), "collections": (COLLECTIONS, "[collections]"), "projects": (PROJECTS, "[projects]")}


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
    return 0


if __name__ == "__main__":
    sys.exit(main())
