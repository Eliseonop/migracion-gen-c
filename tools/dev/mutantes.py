"""Pruebas de mutación: cada mutante de un grupo debe hacer fallar los tests de su etiqueta.

    python tools/dev/mutantes.py <grupo> [preset] [filtro]

`grupo`: eval (evaluación por clip), layers (capas de un instante), frame (fotograma), sfx
(biblioteca de SFX en disco), collections (colecciones de material), projects (almacén de
proyectos), segments (segmentos por referencia y caras), json (JSON como Python), library («Guardar en la biblioteca»), stick (personajes
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
def _clion() -> Path:
    """CLION_HOME o, si no está, el CLion más nuevo de Program Files\\JetBrains."""
    if os.environ.get("CLION_HOME"):
        return Path(os.environ["CLION_HOME"])
    found = sorted(Path(r"C:\Program Files\JetBrains").glob("CLion *"))
    return found[-1] if found else Path(r"C:\Program Files\JetBrains\CLion")


CLION = _clion()
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

SG = "src/core/src/segments.cpp"
SEGMENTS = [
    ("índice nuevo desde 99998", SG, "std::int64_t index = 99999;", "std::int64_t index = 99998;"),
    ("ventana de dur/10", SG, "dur / 12.0 > 1.0 ? dur / 12.0 : 1.0", "dur / 10.0 > 1.0 ? dur / 10.0 : 1.0"),
    ("sin zona muerta", SG, "&& std::abs(cy - out.back().cy) < kDeadzone) {", "&& false) {"),
    ("el central en vez de la mediana", SG,
     "return n % 2 == 1 ? v[n / 2] : (v[(n / 2) - 1] + v[n / 2]) / 2;", "return v[n / 2];"),
    ("sin el ancla del final", SG, "if (out.back().t < end - 0.01) {", "if (false) {"),
    ("caché sin tolerancia", SG, "constexpr double kCacheEps = 0.05;", "constexpr double kCacheEps = 0.0;"),
    ("sin acotar al final del archivo", SG, "b = file_dur < b ? file_dur : b; // min(b, file_dur)", ""),
    ("mínimo de 0,2 s", "src/core/include/gc/core/segments.hpp", "kMinSegment = 0.3;", "kMinSegment = 0.2;"),
    ("padre: el id antes que el padre", SG, "src->parent_id && !src->parent_id->empty() ? *src->parent_id",
     "false ? *src->parent_id"),
    ("etiqueta sin quitar espacios", SG, "return py_truthy(v) ? py_strip(py_str(v)) : std::string();",
     "return py_truthy(v) ? py_str(v) : std::string();"),
    ("m:ss truncando", SG, "static_cast<std::int64_t>(py_round(seconds, 0))", "static_cast<std::int64_t>(seconds)"),
    ("reframe sin guardar", SG, "if (std::abs(a - own_a) <= kCacheEps && std::abs(b - own_b) <= kCacheEps) {",
     "if (false) {"),
    ("rango al revés sin girar", SG, "if (b < a) {\n            std::swap(a, b);", "if (false) {\n            std::swap(a, b);"),
    ("descripción sin la del origen", SG, "description.empty() ? src->description : std::optional(description)",
     "std::optional(description)"),
]

YH = "src/core/src/yt_history.cpp"
YT_HISTORY = [
    ("límite mínimo 2", YH, "(limit > 1 ? limit : 1)", "(limit > 2 ? limit : 2)"),
    ("límite 0 sin el 40", YH, "limit == 0 ? kYtHistoryLimit :", "false ? kYtHistoryLimit :"),
    ("la url antes que el vídeo", YH, "return id.empty() ? py_strip", "return !id.empty() ? py_strip"),
    ("sin quitar por url", YH, "key_of(x) != key && url_of(x) != url", "key_of(x) != key"),
    ("título falso sin la url", YH, "py_truthy(title) ? py_str(title) : url", "py_str(title)"),
    ("key del vídeo sin espacios", YH, "const std::string key = video_id.empty() ? u : video_id;",
     "const std::string key = py_strip(video_id.empty() ? u : video_id);"),
    ("un «at» malo se salta", YH, "return invalid(\"yt_history: «at» no es un entero\");", "continue;"),
    ("al principio no, al final", YH, "out.insert(out.begin(), std::move(entry));", "out.push_back(std::move(entry));"),
    ("remove vacío no guarda", YH, "Json out = needle.empty() ? std::move(*items) : without(*items, needle, needle);",
     "Json out = needle.empty() ? std::move(*items) : without(*items, needle, needle);\n"
     "    if (needle.empty()) {\n        return out;\n    }"),
]

HM = "src/core/src/heatmap.cpp"
HEATMAP = [
    ("umbral estricto", HM, "if (p.value >= min_score) {", "if (p.value > min_score) {"),
    ("sin ordenar por inicio", HM, "std::ranges::stable_sort(points,", "(void)std::ranges::stable_sort; (void)("),
    ("orden no estable", HM, "std::ranges::stable_sort(segments, [](const HeatSegment& x, const HeatSegment& y) { return x.score > y.score; });",
     "std::ranges::sort(segments, [](const HeatSegment& x, const HeatSegment& y) { return x.score >= y.score; });"),
    ("sin acotar a 0", HM, "const double start = a > 0.0 ? a : 0.0;", "const double start = a;"),
    ("sin acotar a la duración", HM, "double end = b < duration ? b : duration;", "double end = b;"),
    ("recorte desde el final", HM, "end = start + max_duration;", "end = end - max_duration;"),
    ("duración con lo redondeado", HM, "py_round(end - start, 2)});", "py_round(py_round(end, 2) - py_round(start, 2), 2)});"),
    ("score con 3 decimales", HM, "py_round(score, 4)", "py_round(score, 3)"),
    ("max_clips negativo como 0", HM, "max_clips < 0 ? n + max_clips : max_clips", "max_clips"),
    ("índices desde 0", HM, "static_cast<std::int64_t>(i) + 1;", "static_cast<std::int64_t>(i);"),
    ("el último grupo se pierde", HM, "    if (!current.empty()) {\n        runs.push_back(std::move(current));\n    }\n\n",
     "\n"),
]

PU = "src/core/src/py_url.cpp"
YU = "src/core/src/youtube_url.cpp"
URL = [
    ("sin quitar los C0 delante", PU, "static_cast<unsigned char>(raw[lead]) <= 0x20", "static_cast<unsigned char>(raw[lead]) < 0x20"),
    ("sin quitar tabuladores", PU, r"if (c != '\t' && c != '\r' && c != '\n') {", r"if (c != '\r' && c != '\n') {"),
    ("esquema con «_»", PU, "c == '+' || c == '-' || c == '.'; }", "c == '+' || c == '-' || c == '.' || c == '_'; }"),
    ("netloc hasta «;»", PU, "rest.find_first_of(\"/?#\", 2)", "rest.find_first_of(\"/?#;\", 2)"),
    ("sin params", PU, "if (std::ranges::find(kUsesParams, p.scheme) != kUsesParams.end() && rest.contains(';')) {", "if (false) {"),
    ("params desde el primer «;»", PU, "i = rest.find(';', slash);", "i = rest.find(';');"),
    ("sin comprobar NFKC", PU, "if (!valid_netloc_nfkc(p.netloc)) {", "if (false) {"),
    ("IPv4 con ceros delante", PU, "|| (o != \"0\" && o[0] == '0')", ""),
    ("IPvFuture sin hex", PU, "return i > 1 && i < h.size()", "return i < h.size()"),
    ("hostname sin el usuario", PU, "const std::string_view hp = rpartition_after(netloc, '@');\n    const auto [_, have", "const std::string_view hp = netloc;\n    const auto [_, have"),
    ("hostname con la zona en minúsculas", PU, "std::string out = py_lower(name);\n    if (pct) {\n        out += '%';\n        out += zone;\n    }", "std::string out = py_lower(host);"),
    ("qsl con valores vacíos", PU, "if (!value.empty()) {\n            out.emplace_back", "if (true) {\n            out.emplace_back"),
    ("qsl sin descodificar el nombre", PU, "out.emplace_back(py_unquote_plus(name), py_unquote_plus(value));", "out.emplace_back(std::string(name), py_unquote_plus(value));"),
    ("«+» sin cambiar", PU, "std::ranges::replace(t, '+', ' ');", ""),
    ("unquote sin trozos no ASCII", PU, "(static_cast<unsigned char>(s[j]) < 0x80) == ascii", "true"),
    ("UTF-8: E0 80 vale", PU, "lo = b == 0xE0 ? 0xA0 : 0x80;", "lo = 0x80;"),
    ("UTF-8: surrogados valen", PU, "hi = b == 0xED ? 0x9F : 0xBF;", "hi = 0xBF;"),
    ("UTF-8: un FFFD por byte", PU, "            out += \"\\xEF\\xBF\\xBD\";\n            i += k;", "            out += \"\\xEF\\xBF\\xBD\";\n            i += 1;"),
    ("youtube: sin quitar www.", YU, "if (host.starts_with(\"www.\")) {", "if (false) {"),
    ("youtube: sin subdominios", YU, "|| ends_with(host, \".youtube.com\");", ";"),
    ("youtube: sin https delante", YU, "raw = \"https://\" + raw;", "raw = raw;"),
    ("descarga: sin lstrip", "src/core/src/asset_import.cpp", "substr(std::min(host.find_first_not_of(\"w.\"), host.size()))", "substr(0)"),
    ("descarga: solo https", "src/core/src/asset_import.cpp", "(parsed->scheme != \"http\" && parsed->scheme != \"https\")", "parsed->scheme != \"https\""),
    ("descarga: subdominio sin punto", "src/core/src/asset_import.cpp", " && h[h.size() - d.size() - 1] == '.'", ""),
    ("id: sin live/", YU, "|| segs[0] == \"live\" ", ""),
]

MU = "src/core/src/media_upgrade.cpp"
MEDIA_UPGRADE = [
    ("proporción al 5 %", MU, "constexpr double kAspectTol = 0.02;", "constexpr double kAspectTol = 0.05;"),
    ("sin margen de 1 s al final", MU, "constexpr double kRangeTol = 1.0;", "constexpr double kRangeTol = 0.0;"),
    ("empieza antes sin margen", MU, "if (start < -0.05) {", "if (start < 0.0) {"),
    ("master no cuenta", MU, "if (!rf || rf->master) {", "if (!rf) {"),
    ("sin mirar el zoom de los keyframes", MU, "return std::ranges::all_of(rf->keyframes,", "return true || std::ranges::all_of(rf->keyframes,"),
    ("sin mirar el zoom", MU, "if (!(rf->zoom >= 0.999)) {", "if (false) {"),
    ("usos de la biblioteca también", MU, "(c.asset_scope.empty() || c.asset_scope == \"project\")", "true"),
    ("orden sin los usos", MU, "        if (ua != ub) {\n            return ua > ub;\n        }\n", ""),
    ("lower con <=", MU, "q.get<std::int64_t>() < new_q.get<std::int64_t>()", "q.get<std::int64_t>() <= new_q.get<std::int64_t>()"),
    ("id: sin source_url", MU, "if (clip.source_url && !clip.source_url->empty() && is_youtube_url(*clip.source_url)) {", "if (false) {"),
    ("parent_id: el índice", MU, "fields[\"parent_id\"] = next.id && !next.id->empty() ? *next.id : std::to_string(next.index);", "fields[\"parent_id\"] = std::to_string(next.index);"),
    ("sin mover el face_track", MU, "        if (c.face_track) {", "        if (false) {"),
    ("dueño: el propio segmento", MU, "                owner = f != nullptr ? f : c;", "                owner = c;"),
    ("sin quitar repetidos", MU, "if (std::ranges::none_of(olds, [&](const ClipInfo* o) { return to_json(*o) == key; })) {", "if (true) {"),
    ("op aunque nadie lo use", MU, "return uses_of(uses, o->filename) != 0; })) {", "return true; })) {"),
    ("fuente sin quitar espacios", MU, "out[*vid] = detail::py_strip(url);", "out[*vid] = std::string(url);"),
    ("fuente sin el enlace watch", MU, ": \"https://www.youtube.com/watch?v=\" + *vid;", ": *vid;"),
    ("mejor: con los parciales", MU, "|| material_file_offset(c) > 0.05) {", ") {"),
    ("mejor: sin mirar el provider", MU, "|| c.provider != \"youtube\" ", ""),
    ("mejor: el último de los iguales", MU, "qv > best_q.value_or(0)", "qv >= best_q.value_or(0)"),
    ("offset de las versiones redondeado", MU, "{\"offset\", material_file_offset(*o)}", "{\"offset\", py_round(material_file_offset(*o), 0)}"),
]

YV = "src/core/src/youtube_video.cpp"
YOUTUBE_VIDEO = [
    ("nota: 4 cifras antes que 3, no", YV, "for (std::size_t len = run; len >= 3; --len) {", "for (std::size_t len = 3; len >= 3 && len <= run; --len) {"),
    ("nota: dígitos solo ASCII", YV, "py_isdecimal(cps[i + run])", "(cps[i + run] >= U'0' && cps[i + run] <= U'9')"),
    ("calidad: el ancho", YV, "return *h != 0 ? std::optional<std::int64_t>(*h) : std::nullopt;", "return *w != 0 ? std::optional<std::int64_t>(*w) : std::nullopt;"),
    ("sin quitar los storyboards", YV, "|| or_default(field(f, \"ext\"), kEmpty) == Json(\"mhtml\")", ""),
    ("sin quitar el DRM", YV, "py_truthy(field(f, \"has_drm\")) || ", ""),
    ("HDR antes que SDR", YV, "        return a.sdr;\n", "        return b.sdr;\n"),
    ("HLS antes", YV, "        return a.direct;\n", "        return b.direct;\n"),
    ("códec sin orden", YV, "        return a.codec > b.codec;\n", "        return a.codec < b.codec;\n"),
    ("max: el último de los iguales", YV, "if (best == nullptr || greater(*r, best_rank)) {", "if (best == nullptr || !greater(best_rank, *r)) {"),
    ("audio: sin preferir m4a", YV, "or_default(field(f, \"ext\"), kEmpty) == Json(\"m4a\"),", "false,"),
    ("tamaño sin el aproximado", YV, "return int_of(size != 0.0 ? size : pnum(field(*f, \"filesize_approx\")));", "return int_of(size);"),
    ("fps redondeando hacia arriba", YV, "std::nearbyint(pnum(field(*f, \"fps\")))", "std::round(pnum(field(*f, \"fps\")))"),
    ("sin el mínimo de 360", YV, "if (q >= kYoutubeMinQuality) {", "if (q >= 0) {"),
    ("resolve: sin la peor", YV, "    return avail.back();\n}", "    return avail.front();\n}"),
    ("spec: sin el audio aparte", YV, "if (or_default(field(f, \"acodec\"), kEmpty) != kNone) {", "if (true) {"),
    ("MB: siempre un decimal", YV, "return mb < 10 ? std::format", "return true ? std::format"),
    ("protocolo raro sin error", YV, "        return invalid(\"protocol no es iterable\");\n", ""),
]

FB = "src/core/src/fx_bake.cpp"
FX_BAKE = [
    ("enabled 0 también fuera", FB, "get(e, \"enabled\") == Json(false)", "!py_truthy(get(e, \"enabled\"))"),
    ("from negativo", FB, "const double from = f > 0.0 ? f : 0.0;", "const double from = f;"),
    ("to sin el mínimo", FB, "to = *t > from ? *t : from;", "to = *t;"),
    ("v sin la del efecto", FB, "const Json& pick = py_truthy(raw_v) ? raw_v : man_v;", "const Json& pick = man_v;"),
    ("v mala sin error", FB, "            if (!as_int) {", "            if (false) {"),
    ("uid sin texto", FB, "py_truthy(get(e, \"uid\")) ? py_str(get(e, \"uid\")) : std::string()", "std::string()"),
    ("texto con efectos", FB, "constexpr std::array<std::string_view, 3> kFxKinds{\"video\", \"image\", \"motion\"};",
     "constexpr std::array<std::string_view, 4> kFxKinds{\"video\", \"image\", \"motion\", \"text\"};"),
    ("margen sin tope", FB, "return total < 0.5 ? total : 0.5;", "return total;"),
    ("margen sin mirar el final", FB, "constexpr std::array<double, 3> kAt{0.0, 0.5, 1.0};", "constexpr std::array<double, 2> kAt{0.0, 0.5};"),
    ("margen en un clip lleno", FB, "if (!is_overlay(clip) || iw <= 0", "if (iw <= 0"),
    ("encuadre sin el revés", FB, "clip.reverse ? (out - k.t) / spd : (k.t - inp) / spd", "(k.t - inp) / spd"),
    ("encuadre sin la velocidad", FB, "(k.t - inp) / spd;", "(k.t - inp);"),
    ("encuadre negativo", FB, "k.t = py_round(t > 0.0 ? t : 0.0, 4);", "k.t = py_round(t, 4);"),
    ("horneado con su bg", FB, "c.bg_removal = std::nullopt;", ""),
    ("imagen sigue siendo imagen", FB, "        c.kind = \"video\";\n", ""),
    ("horneado con su velocidad", FB, "    c.speed = 1.0;\n", ""),
]

MD = "src/core/src/mcp_dto.cpp"
MC = "src/core/src/mcp_capabilities.cpp"
MR = "src/core/src/mcp_registry.cpp"
MA = "src/core/src/mcp_audit.cpp"
MS = "src/core/src/mcp_resources.cpp"
MH = "src/core/src/mcp_help.cpp"
MCP = [
    ("dto: -0,0 no pasa a 0,0", MD, "return x == 0.0 ? 0.0 : x;", "return x;"),
    ("dto: max que se queda el NaN", MD, "return b > a ? b : a;", "return a > b ? a : b;"),
    ("dto: velocidad negativa divide", MD, "return speed > 0 ? span / speed : span;", "return span / speed;"),
    ("dto: imagen con velocidad", MD, 'if (clip.kind == "text" || clip.kind == "image") {', 'if (clip.kind == "text") {'),
    ("dto: dup_of vacío es linaje", MD, "c.dup_of && !c.dup_of->empty() ? *c.dup_of : c.id", "c.dup_of ? *c.dup_of : c.id"),
    ("dto: pista sin tipo gana", MD, "track_kind != nullptr && !track_kind->empty() ? *track_kind : c.kind",
     "track_kind != nullptr ? *track_kind : c.kind"),
    ("dto: every por defecto 0", MD, "every != beats.end() ? *every : Json(1)", "every != beats.end() ? *every : Json(0)"),
    ("dto: aspecto al 0,05 %", MD, "constexpr double kRatioTol = 0.01;", "constexpr double kRatioTol = 0.0005;"),
    ("dto: etiqueta vacía no cae al archivo", MD, "c.label && !c.label->empty() ? *c.label : c.filename",
     "c.label ? *c.label : c.filename"),
    ("dto: done también cancelado", MD, 'py_truthy(get(job, "cancel_requested")) && status != "done"',
     'py_truthy(get(job, "cancel_requested"))'),
    ("dto: result.update mueve las claves", MD, "result[key] = value; // dict.update",
     "result.erase(key); result[key] = value; // dict.update"),
    ("dto: magnética siempre", MD, "if (t.magnetic) {", "if (true) {"),
    ("capacidades: describe sin strip", MC, "detail::py_lower(detail::py_strip(domain))", "detail::py_lower(std::string(domain))"),
    ("capacidades: describe sin lower", MC, "detail::py_lower(detail::py_strip(domain))", "detail::py_strip(domain)"),
    ("capacidades: verbos sin ordenar", MC, "        std::ranges::sort(sorted);\n", ""),
    ("capacidades: available sin la verdad de Python", MC, 'detail::py_truthy(e["available"])', 'e["available"] == true'),
    ("registro: hint vacío sale", MR, "if (hint && !hint->empty()) {", "if (hint) {"),
    ("registro: clasificar sin minúsculas", MR, "const std::string m = detail::py_lower(message);", "const std::string m(message);"),
    ("registro: id de job que no es texto", MR, "id->is_string() && ", ""),
    ("auditoría: ms sin redondear", MA, 'entry["ms"] = detail::py_round(*args.ms, 1);', 'entry["ms"] = *args.ms;'),
    ("auditoría: error sin cortar", MA, "detail::py_slice(*args.error, kErrorMax)", "*args.error"),
    ("auditoría: progreso 0 real", MA, "job->progress == 0.0 ? Json(0) : Json(job->progress)", "Json(job->progress)"),
    ("auditoría: splitlines sin U+2028", MA, "(static_cast<unsigned char>(s[i + 2]) == 0xA8 || ", "("),
    ("auditoría: hora redondeada", MA, "floor<milliseconds>(t - day)", "round<milliseconds>(t - day)"),
    ("auditoría: read_recent hasta 2000", MA, ", 1, 200);", ", 1, 2000);"),
    ("auditoría: param_keys sin ordenar", MA, "    std::ranges::sort(out); // por bytes", "    // por bytes"),
    ("auditoría: fin de datos como continuación", MA, 'return {0, s.size() - i, "unexpected end of data"};',
     'return {0, 1, "invalid continuation byte"};'),
    ("resources: clip con otro texto", MS, '"Clip no encontrado en la timeline: "', '"Clip no encontrado: "'),
    ("resources: sin historial", MS, "ctx.history ? ctx.history(pid) : History()", "History()"),
    ("ayuda: dominios repetidos", MH, "    out.erase(first, last);\n", ""),
]

MI = "src/core/src/media_import.cpp"
IMPORT = [
    ("extensión sin minúsculas", MI, "return detail::py_lower(detail::py_path_suffix(filename));",
     "return detail::py_path_suffix(filename);"),
    ("índice desde 0", MI, "std::int64_t top = 99999;", "std::int64_t top = 0;"),
    ("etiqueta vacía cuenta", MI, "return v && !v->empty() ? *v : std::string(fallback);",
     "return v ? *v : std::string(fallback);"),
    ("url sin quote", MI, "detail::py_quote(placed->dest_name)", "placed->dest_name"),
    ("sonda que falla da 1", MI, "probe(*dest).value_or(0.0)", "probe(*dest).value_or(1.0)"),
    ("vídeo vacío sin error", MI, "    if (data.empty()) {", "    if (false) {"),
    ("nombre sin sanear", MI, "safe_file_name(detail::py_path_stem(filename))", "std::string(detail::py_path_stem(filename))"),
    ("audio con el nombre guardado", MI, "info.label = detail::py_path_stem(filename);", "info.label = placed->dest_name;"),
    ("origen por defecto upload", MI, 'or_text(meta.origin, "import")', 'or_text(meta.origin, "upload")'),
    ("audio con extensiones de vídeo", MI, "if (!has(kImportAudioExts, lower_suffix(filename))) {",
     "if (!has(kImportVideoExts, lower_suffix(filename))) {"),
    # images.import_image
    ("imagen: nombre sin unquote", MI, "detail::py_path_name(detail::py_unquote(filename))", "detail::py_path_name(filename)"),
    ("imagen: sin quitar la consulta", MI, "    name = name.substr(0, name.find('?'));\n", ""),
    ("imagen: extensión rara sin error", MI, "if (!ext.empty() && guessed.empty()) {", "if (false) {"),
    ("imagen: el GIF se aplana", MI, "if (keep_gif && is_gif) {", "if (false) {"),
    ("imagen: el PNG se reconvierte", MI, "data.starts_with(kPngMagic) ? std::optional(std::string(data))",
     "false ? std::optional(std::string(data))"),
    ("imagen: un 0 de probe_size cuenta", MI, "if (!w || *w == 0) {", "if (!w) {"),
    ("imagen: origen import", MI, 'or_text(meta.origin, "upload")', 'or_text(meta.origin, "import")'),
    ("imagen: descripción vacía cuenta", MI, "meta.description && !meta.description->empty() ? meta.description",
     "meta.description ? meta.description"),
]

CLU = "src/core/src/clip_layout_ui.cpp"
# Equivalentes (no están): no acotar el tiempo local (antes del primer keyframe vale lo mismo en −1
# que en 0) y no vaciar los keyframes al poner la altura completa (solo pasa si ya estaban vacíos).
FREE_LAYOUT = [
    ("libre: el doble encuadre también", CLU, "if (!visual_kind(clip) || (clip.reframe && clip.reframe->dual_crop)) {",
     "if (!visual_kind(clip)) {"),
    ("libre: un overlay pegado ya es libre", CLU, 'return !(is_overlay(clip) && (clip.frame.empty() || clip.frame == "free"));',
     "return !is_overlay(clip);"),
    ("libre: tolerancia del zoom 0,01", CLU, "return !(std::abs(rf.zoom - 1) > 0.001);", "return !(std::abs(rf.zoom - 1) > 0.01);"),
    ("libre: con keyframes de encuadre a altura completa", CLU, "    if (!rf.keyframes.empty()) {\n        return false;\n    }\n", ""),
    ("libre: altura completa por el ancho", CLU, '{"scale", js_fixed(out_h / src_h, 5)}', '{"scale", js_fixed(out_w / src_w, 5)}'),
    ("libre: llenar con min", CLU, "std::max(out_w / std::max(1.0, px.sw), out_h / std::max(1.0, px.sh))",
     "std::min(out_w / std::max(1.0, px.sw), out_h / std::max(1.0, px.sh))"),
    ("libre: el overlay recortado se reencuadra", CLU,
     "if (is_overlay(clip) && clip.reframe && clip.reframe->crop_w && clip.reframe->crop_h) {", "if (false) {"),
    ("libre: escala a 4 decimales", CLU, "{\"scale\", js_fixed(scale, 5)}", "{\"scale\", js_fixed(scale, 4)}"),
]

MU = "src/core/src/mask_ui.cpp"
MASK_UI = [
    ("máscara: el centro sin el origen del recuadro", MU, '.cx = frame.x + (num(mask, "x", 0) * frame.w),',
     '.cx = num(mask, "x", 0) * frame.w,'),
    ("máscara: semiejes sin el mínimo de 0,5", MU, '.hw = std::max(0.5, (num(mask, "w", 0) * num(mask, "scale_x", 0) * ref) / 2),',
     '.hw = (num(mask, "w", 0) * num(mask, "scale_x", 0) * ref) / 2,'),
    ("máscara: el giro en grados", MU, '.rot = num_or(mask, "rotation", 0) * std::numbers::pi / 180,',
     '.rot = num_or(mask, "rotation", 0),'),
    ("máscara: lo local sin deshacer el giro", MU, "const double s = std::sin(-g.rot);", "const double s = std::sin(g.rot);"),
    ("máscara: la división con la caja de las demás", MU, 'if (type == "linear") {', 'if (type == "lineal") {'),
    ("máscara: la caja de la división sin su alto mínimo", MU, "g.hh = std::max(18.0, frame.h * 0.035);",
     "g.hh = frame.h * 0.035;"),
    ("máscara: el rollo con la caja de las demás", MU, '} else if (type == "film") {', '} else if (type == "filme") {'),
    ("máscara: la separación de los tiradores fija", MU, "const double pad = std::max(16.0, frame.h * 0.022);",
     "const double pad = 16.0;"),
    ("máscara: el tirador de giro sin separación", MU, "at(0, -g.hh - pad)", "at(0, -g.hh)"),
    ("máscara: la pluma antes que el giro", MU,
     '    if (near(h.rot)) {\n        return "rotate";\n    }\n    if (near(h.fea)) {\n        return "feather";\n    }\n',
     '    if (near(h.fea)) {\n        return "feather";\n    }\n    if (near(h.rot)) {\n        return "rotate";\n    }\n'),
    ("máscara: la esquina después del ancho", MU,
     '    if (near(h.br)) {\n        return "corner";\n    }\n    if (near(h.r)) {\n        return "width";\n    }\n',
     '    if (near(h.r)) {\n        return "width";\n    }\n    if (near(h.br)) {\n        return "corner";\n    }\n'),
    ("máscara: dentro sin la unidad", MU, "std::abs(local.x) * g.ref <= g.hw", "std::abs(local.x) <= g.hw"),
    ("máscara: mover sin acotar", MU, 'js_fixed(clamp(num(mask, "x", 0) + ((p.x - p0.x) / frame.w), -1, 2), 4)',
     'js_fixed(num(mask, "x", 0) + ((p.x - p0.x) / frame.w), 4)'),
    ("máscara: girar sin el módulo", MU,
     'std::fmod(num_or(mask, "rotation", 0) + ((ang - ang0) * 180 / std::numbers::pi), 360)',
     '(num_or(mask, "rotation", 0) + ((ang - ang0) * 180 / std::numbers::pi))'),
    ("máscara: la pluma sin su tope", MU, "0, kMaskFeatherMax), 4)", "0, 1.0), 4)"),
    ("máscara: el ancho sin la escala", MU, 'std::abs(l.x) * 2 / num_or(mask, "scale_x", 1)', "std::abs(l.x) * 2"),
    ("máscara: la esquina sin el alto", MU, 'if (mode == "height" || mode == "corner") {', 'if (mode == "height") {'),
    ("pincel: el primer punto sin marcar", MU, 'pt["m"] = 1;', 'pt["m"] = 0;'),
    ("pincel: sin quitar los pasos cortos", MU, "0.004) {", "0.0) {"),
    ("pincel: sin la escala", MU, 'js_fixed(l.x / num_or(mask, "scale_x", 1), 4)', "js_fixed(l.x, 4)"),
    ("cursor: la esquina con el eje del ancho", MU, '*mode == "corner" ? 45 : ', '*mode == "corner" ? 0 : '),
    ("cursor: sin el giro de la máscara", MU,
     "return resize_cursor(axis + (mask_handle_box(mask, frame).rot * 180 / std::numbers::pi));",
     "return resize_cursor(axis);"),
    ("cursor: el pincel en cualquier máscara", MU, 'if (drawing && type_of(mask) == "brush") {', "if (drawing) {"),
    ("cursor: la flecha redondeada hacia abajo", MU, "std::round(a / 45)", "std::floor(a / 45)"),
    ("estrella: sin el mínimo de 3 puntas", MU, "const int n = std::max(3, spikes);", "const int n = spikes;"),
    ("corazón: el alto normalizado por el ancho", MU, "(max_y.y - min_y.y)", "(max_x.x - min_x.x)"),
]

CU = "src/core/src/captions_ui.cpp"
# Equivalentes (no están): un texto vuelve a dar «no» por no ser vídeo; un nombre vacío no llega (el modelo lo
# deja sin nombre); el final de una palabra nunca queda antes de su inicio (los dos se recortan igual), y sin
# su final cuenta lo mismo caer al inicio que a 0 (end = max(inicio, fin)); la duración mínima de 0,4 s no
# cambia nada: makeTextClip ya pone 0,5.
CAPTIONS_UI = [
    ("subtítulos: sin los SFX", CU, ' || clip.asset_kind == "sfx"', ""),
    ("subtítulos: los de la biblioteca también", CU, 'if (scope == "library" || scope == "collection") {',
     'if (scope == "collection") {'),
    ("subtítulos: sin material también", CU, "return !clip.asset_id.empty();", "return true;"),
    ("subtítulos: la referencia sin el nombre", CU, '        ref["name"] = *clip.name;\n', ""),
    ("subtítulos: max_words sin su defecto", CU, "const Json max_words = given != nullptr ? *given : Json(8);",
     "const Json max_words = given != nullptr ? *given : Json();"),
    ("subtítulos: los segmentos de antes", CU, "if (le <= 0 || ls >= clip_len) {", "if (ls >= clip_len) {"),
    ("subtítulos: el tramo sin recortar por delante", CU, "const double start = src.start + std::max(0.0, ls);",
     "const double start = src.start + ls;"),
    ("subtítulos: el tramo sin recortar por detrás", CU, "const double end = src.start + std::min(clip_len, le);",
     "const double end = src.start + le;"),
    ("subtítulos: sin el rol de subtítulo", CU, '        made["text_role"] = "caption";\n', ""),
    ("subtítulos: el índice nulo cuenta", CU, 'const Json* index = present(s, "index");',
     'const Json* index = s.contains("index") ? &s.at("index") : nullptr;'),
    ("subtítulos: sin la referencia al material", CU, '            origin["source_ref"] = source_ref;\n', ""),
    ("subtítulos: sin la transcripción", CU, '{"transcript_id", transcript_id != nullptr ? *transcript_id : Json()}',
     '{"transcript_id", Json()}'),
    ("palabras: sin recortar al clip", CU, "const double rs = std::min(std::max(0.0, tl_start - clip_start), dur);",
     "const double rs = tl_start - clip_start;"),
    ("palabras: sin la probabilidad", CU, '            item["prob"] = *prob;\n', ""),
    ("palabras: sin quitar los espacios", CU,
     "return v != nullptr && v->is_string() ? js_trim(v->get<std::string>()) : std::string();",
     "return v != nullptr && v->is_string() ? v->get<std::string>() : std::string();"),
]

FF = "src/core/src/face_follow.cpp"
CPE = "src/core/src/clip_pose_edit.cpp"
# Equivalentes (no están): la velocidad mínima de 0,01 (las del caso son positivas) y volver a encender unos
# keyframes que ya lo estaban (enableKeyframes los deja igual).
FACE_FOLLOW = [
    ("seguir: sin la velocidad", FF, "const double local = clip.reverse ? (clip.out_point - p.t) / sp : (p.t - clip.in_point) / sp;",
     "const double local = clip.reverse ? (clip.out_point - p.t) : (p.t - clip.in_point);"),
    ("seguir: sin el revés", FF, "const double local = clip.reverse ? (clip.out_point - p.t) / sp : (p.t - clip.in_point) / sp;",
     "const double local = (p.t - clip.in_point) / sp;"),
    ("seguir: sin la separación mínima", FF, "if (!out.empty() && local - out.back().t < min_gap) {", "if (false) {"),
    ("seguir: fuera del tramo también", FF, "if (at < clip.in_point - 1e-6 || at > clip.out_point + 1e-6) {", "if (false) {"),
    ("seguir: sin ordenar", FF, "    std::ranges::stable_sort(pts, {}, &P::t);\n", ""),
    ("seguir: cy sin su defecto", FF, ": 0.5}); // p.cy ?? 0.5", ": 0.0}); // p.cy ?? 0.5"),
    ("seguir: x a 3 decimales", FF, "js_fixed(o.x, 4)", "js_fixed(o.x, 3)"),
    ("seguir: sin encender los keyframes", FF, "        enable_keyframes(clip, keys.front().t, src_t, ids);\n", ""),
    ("keyframes: el primero sin el encuadre del archivo", CPE,
     "const Frame fr = frame_at(snap.reframe->keyframes, src_t, snap.reframe->zoom, snap.reframe->pan_mode);",
     "const Frame fr = frame_at(snap.reframe->keyframes, local_t, snap.reframe->zoom, snap.reframe->pan_mode);"),
    ("keyframes: el primero sin ajustar al fotograma", CPE, '{"t", js_fixed(snap_to_frame(local_t, 30), 6)}',
     '{"t", js_fixed(local_t, 6)}'),
]

TR = "src/core/src/mcp_tools_read.cpp"
MCP_TOOLS = [
    ("resolver: sin quitar los espacios", TR, "const std::string q = py_strip(raw);", "const std::string q = raw;"),
    ("resolver: sin minúsculas", TR, "const std::string ql = py_lower(q);", "const std::string ql = q;"),
    ("resolver: el nombre exacto sin strip", TR, "return py_lower(py_strip(p.name)) == ql;", "return py_lower(p.name) == ql;"),
    ("resolver: prefijo del id en mayúsculas", TR, "return py_lower(p.id).starts_with(ql);", "return p.id.starts_with(ql);"),
    ("resolver: sin quitar ids repetidos", TR, "if (matches(p) && seen.insert(p.id).second) {", "if (matches(p)) {"),
    ("resolver: más de 10 candidatos", TR, "i < uniq.size() && i < 10;", "i < uniq.size();"),
    ("resolver: el id exacto no gana", TR, "        if (p.id == q) {\n", "        if (false) {\n"),
    ("historial: un texto no cuenta", TR, "        return py_len(v.get_ref<const std::string&>());", "        return std::size_t{0};"),
    ("historial: un dict no cuenta", TR, "    if (v.is_array() || v.is_object()) {\n        return v.size();", "    if (v.is_array()) {\n        return v.size();"),
    ("historial: claves repetidas", TR, "                seen = true; // la clave", "                seen = false; // la clave"),
    ("historial: NaN distintos", TR, "std::isnan(b.get<double>())) {\n        return true;", "std::isnan(b.get<double>())) {\n        return false;"),
    ("historial: un par de tres vale", TR, "if (pair->size() != 2) {", "if (pair->size() < 2) {"),
    ("historial: lo que no es dict, vacío", TR, "    if (!h.is_object()) {\n        return crash(", "    if (!h.is_object()) {\n        return history_summary(Json::object());\n        return crash("),
    ("historial: ilegible es un error", TR, "return history_summary(raw ? *raw : Json(nullptr));", "if (!raw) { return std::unexpected(raw.error()); }\n    return history_summary(*raw);"),
    ("buscar: sin minúsculas en el texto", TR, "return py_lower(seg.text).contains(q)", "return seg.text.contains(q)"),
    ("buscar: sin los clips", TR, "        if (!c.transcript) {\n            continue;", "        if (true) {\n            continue;"),
    ("buscar: límite 0 vale", TR, "*n < 1 ? 1 :", "*n < 0 ? 0 :"),
    ("buscar: el NaN es un crash", TR, 'return value_error("cannot convert float NaN to integer");', 'return crash("ValueError", "nan");'),
    ("buscar: cuenta las que se dan", TR, "const std::size_t count = matches.size();\n    if (matches.size() > keep) {\n        matches.erase(matches.begin() + static_cast<std::ptrdiff_t>(keep), matches.end());\n    }",
     "if (matches.size() > keep) {\n        matches.erase(matches.begin() + static_cast<std::ptrdiff_t>(keep), matches.end());\n    }\n    const std::size_t count = matches.size();"),
    ("contexto: sin el historial", TR, "return project_context(*proj, *history);", "return project_context(*proj, nullptr);"),
    ("capacidades: el vacío pide describe", TR, "    if (py_truthy(domain)) {\n        if (!domain.is_string())", "    if (!domain.is_null()) {\n        if (!domain.is_string())"),
]

TE = "src/core/src/mcp_tools_edit.cpp"
MCP_EDIT = [
    ("fps de una timeline nueva sin los ajustes", TE, 'fresh.fps = exported->at("fps").get<std::int64_t>();', "fresh.fps = 30;"),
    ("sin apilar el estado anterior", TE, "history.record(std::make_shared<const Timeline>(std::move(*current)));\n    if (auto saved = persist(env, pid, history, &res->timeline)", "if (auto saved = persist(env, pid, history, &res->timeline)"),
    ("la op no guarda la timeline", TE, "persist(env, pid, history, &res->timeline)", "persist(env, pid, history, nullptr)"),
    ("deshacer y rehacer cambiados", TE, "undo ? history.undo(snapshot) : history.redo(snapshot)", "undo ? history.redo(snapshot) : history.undo(snapshot)"),
    ("sin el aviso de nada que deshacer", TE, '{undo ? "nada que deshacer" : "nada que rehacer"}', "{}"),
    ("el checkpoint no se guarda", TE, "if (auto saved = persist(env, pid, history, nullptr); !saved) {\n        return std::unexpected(saved.error());\n    }\n", ""),
    ("volver al checkpoint no se deshace", TE, "history.record(std::make_shared<const Timeline>(std::move(*current)));\n    if (auto saved = persist(env, pid, history, state.get())", "if (auto saved = persist(env, pid, history, state.get())"),
    ("checkpoint: primero el proyecto", TE, 'const TimelineSnapshot state = history.checkpoint_state(name);\n    if (state == nullptr) {', 'const TimelineSnapshot state = history.checkpoint_state(name);\n    if (auto cur = current_timeline(env, pid); !cur) {\n        return std::unexpected(cur.error());\n    }\n    if (state == nullptr) {'),
    ("material: sin el id del clip", TE, ' && !(c.id && !c.id->empty() && *c.id == id)', ""),
    ("material: un segmento sin desplazar", TE, "info.offset = *c.in_point;", "info.offset = 0.0;"),
    ("material: un segmento dura lo del clip", TE, "info.source_duration = py_round(*c.out_point - *c.in_point, 3);", ""),
    ("material: encuadre sin seguimiento", TE, "if (c.face_track && c.reframe) {", "if (c.reframe) {"),
    ("material: la etiqueta vacía vale", TE, "return item.label && !item.label->empty() ? *item.label : item.filename;", "return item.label ? *item.label : item.filename;"),
    ("material: imagen de 3 s", TE, "constexpr double kImageDefaultDur = 5.0;", "constexpr double kImageDefaultDur = 3.0;"),
    ("material: sin el nombre del SFX", TE, ".name = py_path_stem(id)}", ".name = id}"),
    ("material: asset_kind sin param", TE, '" (usa clips|audios|images|sfx)", "asset_kind")', '" (usa clips|audios|images|sfx)")'),
    ("añadir: out_point igual vale", TE, "if (*op_end <= *ip) {", "if (*op_end < *ip) {"),
    ("añadir: sin redondear", TE, 'clip["in_point"] = py_round(*ip + info->offset, 3);', 'clip["in_point"] = *ip + info->offset;'),
    ("añadir: siempre una pista nueva", TE, "if (it != tracks.end()) {", "if (false) {"),
    ("añadir: otro layout", TE, 'clip["layout"] = "fill";', 'clip["layout"] = "overlay";'),
    ("añadir: sin marcar el segmento", TE, 'clip["ref_segment"] = true;', ""),
    ("añadir: la pista nueva no se cuenta", TE, "changed.insert(changed.begin(), track->first);", ""),
    ("seguir: el vídeo no vale", TE, 'if (audio->kind != "audio" && audio->kind != "video") {', 'if (audio->kind != "audio") {'),
    ("seguir: sin alinear al clip", TE, "py_round(window->first - visual->start + t, 3)", "t"),
    ("seguir: sin el aviso de no solapar", TE, 'out.warnings.emplace_back("el audio no se solapa con el clip; se usa desde el inicio del audio");', ""),
    ("seguir: desde el principio del audio", TE, "window ? audio_src_at(*audio, window->first) : audio->in_point", "audio->in_point"),
    ("animar: intensidad por defecto", TE, 'or_default("intensity", 1.0)', 'or_default("intensity", 0.5)'),
    ("efectos: con los generadores", TE, 'arg(e, "engine") == "generator" || ', ""),
    ("efectos: sin categoría", TE, ' || (py_truthy(category) && arg(e, "category") != category)', ""),
    ("efectos: sin minúsculas en la búsqueda", TE, 'py_split(py_lower(query.is_string()', 'py_split((query.is_string()'),
    ("efectos: con la etiqueta", TE, 'copy.erase("label");', ""),
    ("efectos: sin las tags", TE, 'for (const Json& t : tags) {\n            add(t);\n        }', ""),
    ("op: barras por defecto", TE, '{"ratio", "2.39"}', '{"ratio", "16:9"}'),
    ("op: marcadores con otro nombre", TE, '"set_timeline_markers", "set_markers"', '"set_timeline_markers", "set_marker"'),
    ("op: una lista nula no es []", TE, "if (!py_truthy(v)) {\n            v = Json::array();\n            continue;\n        }", ""),
    ("textos: el ratio sin la lista", "src/core/src/edit_ops_media.cpp", "['2.39', '2', '1.85', '16:9']", "2.39, 2, 1.85, 16:9"),
    ("textos: el efecto sin la pista", "src/core/src/edit_ops_media.cpp", '" (list_effects da el catálogo)");\n    }\n    if (field(*man', '"");\n    }\n    if (field(*man'),
]

GROUPS = {"eval": (EVAL, "[eval]"), "layers": (LAYERS, "[layers]"), "frame": (FRAME, "[frame]"),
          "export_input": (EXPORT_INPUT, "[export_input]"), "audio": (AUDIO, "[audio]"), "filters": (FILTERS, "[eval]"),
          "stick": (STICK, "[stick]"), "motion": (MOTION, "[motion]"), "media": (MEDIA, "[media]"),
          "library": (LIBRARY, "[library],[projects]"),
          "sfx": (SFX, "[sfx]"), "collections": (COLLECTIONS, "[collections]"), "projects": (PROJECTS, "[projects]"), "segments": (SEGMENTS, "[projects]"), "yt_history": (YT_HISTORY, "[settings]"), "heatmap": (HEATMAP, "[heatmap]"), "url": (URL, "[url]"), "media_upgrade": (MEDIA_UPGRADE, "[projects]"), "youtube_video": (YOUTUBE_VIDEO, "[youtube]"), "fx_bake": (FX_BAKE, "[fx]"), "mcp": (MCP, "[mcp]"), "importar": (IMPORT, "[projects]"), "objeto_libre": (FREE_LAYOUT, "[editor]"), "mascara_preview": (MASK_UI, "[editor]"), "subtitulos_editor": (CAPTIONS_UI, "[editor]"), "seguir_cara": (FACE_FOLLOW, "[editor]"), "mcp_tools": (MCP_TOOLS, "[mcp]"), "mcp_edit": (MCP_EDIT, "[mcp]"),
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
