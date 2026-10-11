#include "gc/render/text.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <utility>

#include <hb.h>

#include "gc/core/blend.hpp"
#include "gc/core/masks.hpp"
#include "gc/core/text_events.hpp"
#include "gc/core/text_metrics.hpp"
#include "gc/core/text_style.hpp"
#include "gc/core/timeline_rules.hpp"
#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkTypeface.h"

#ifdef _WIN32
#include "include/ports/SkTypeface_win.h"
#endif

#include "canvas_impl.hpp"

namespace gc::render {

namespace {

// --- Fuentes ---------------------------------------------------------------------------------

// Un TTF cargado: los bytes (HarfBuzz los lee sin copiarlos), el SkTypeface y las métricas de
// línea de libass (set_font_metrics: usWinAscent / usWinDescent de la OS/2 si suman algo; si no,
// las de hhea).
struct FontFace {
    std::vector<char> bytes;
    sk_sp<SkTypeface> typeface;
    hb_face_t* face = nullptr;
    double upem = 1000;
    double asc = 0;  // unidades de la fuente
    double desc = 0; // unidades de la fuente, positivo hacia abajo

    FontFace() = default;
    FontFace(const FontFace&) = delete;
    FontFace& operator=(const FontFace&) = delete;
    ~FontFace() {
        if (face != nullptr) {
            hb_face_destroy(face);
        }
    }
};

int be16(const char* p) {
    return (static_cast<unsigned char>(p[0]) << 8) | static_cast<unsigned char>(p[1]);
}

std::shared_ptr<FontFace> load_face(SkFontMgr& mgr, const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return nullptr;
    }
    auto ff = std::make_shared<FontFace>();
    ff->bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (ff->bytes.empty()) {
        return nullptr;
    }
    ff->typeface = mgr.makeFromData(SkData::MakeWithCopy(ff->bytes.data(), ff->bytes.size()));
    if (!ff->typeface) {
        return nullptr;
    }
    hb_blob_t* blob = hb_blob_create(ff->bytes.data(), static_cast<unsigned>(ff->bytes.size()),
                                     HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    ff->face = hb_face_create(blob, 0);
    hb_blob_destroy(blob);
    ff->upem = hb_face_get_upem(ff->face);
    unsigned len = 0;
    hb_blob_t* os2 = hb_face_reference_table(ff->face, HB_TAG('O', 'S', '/', '2'));
    const char* d = hb_blob_get_data(os2, &len);
    const int win_asc = len >= 78 ? static_cast<std::int16_t>(be16(d + 74)) : 0;
    const int win_desc = len >= 78 ? static_cast<std::int16_t>(be16(d + 76)) : 0;
    const bool has_os2 = len >= 78;
    hb_blob_destroy(os2);
    if (has_os2 && win_asc + win_desc != 0) {
        ff->asc = win_asc;
        ff->desc = win_desc;
    } else {
        hb_blob_t* hhea = hb_face_reference_table(ff->face, HB_TAG('h', 'h', 'e', 'a'));
        const char* h = hb_blob_get_data(hhea, &len);
        if (len >= 8) {
            ff->asc = static_cast<std::int16_t>(be16(h + 4));
            ff->desc = -static_cast<std::int16_t>(be16(h + 6));
        }
        hb_blob_destroy(hhea);
    }
    if (ff->upem <= 0 || ff->asc + ff->desc <= 0) {
        return nullptr;
    }
    return ff;
}

// Una línea con la forma de HarfBuzz a `em` px. Las posiciones van en 1/64 de px, como las de
// libass (hb-ft), con `spacing` px tras cada glifo (\fsp).
struct Shaped {
    std::vector<SkGlyphID> ids;
    std::vector<double> x;               // origen de cada glifo desde el inicio de la línea
    std::vector<double> y;               // desplazamiento vertical (hacia abajo)
    std::vector<std::uint32_t> clusters; // byte del texto de cada glifo
    std::vector<double> advances;        // avance de cada glifo (sin el espaciado)
    double width = 0;                    // avance total
};

Shaped shape(const FontFace& ff, std::string_view text, double em, double spacing, bool kerning) {
    Shaped out;
    hb_font_t* font = hb_font_create(ff.face);
    const int scale = static_cast<int>(std::lround(em * 64));
    hb_font_set_scale(font, scale, scale);
    hb_buffer_t* buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, text.data(), static_cast<int>(text.size()), 0, static_cast<int>(text.size()));
    hb_buffer_guess_segment_properties(buf);
    // libass solo hace kerning con «Kerning: yes» en [Script Info], y el .ass del export no lo pone
    hb_feature_t no_kern{};
    hb_feature_from_string("-kern", -1, &no_kern);
    hb_shape(font, buf, kerning ? nullptr : &no_kern, kerning ? 0 : 1);
    unsigned n = 0;
    const hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &n);
    const hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &n);
    double pen = 0;
    for (unsigned i = 0; i < n; ++i) {
        out.ids.push_back(static_cast<SkGlyphID>(info[i].codepoint));
        out.x.push_back(pen + pos[i].x_offset / 64.0);
        out.y.push_back(-pos[i].y_offset / 64.0);
        out.clusters.push_back(info[i].cluster);
        out.advances.push_back(pos[i].x_advance / 64.0);
        pen += pos[i].x_advance / 64.0 + spacing;
    }
    out.width = pen;
    hb_buffer_destroy(buf);
    hb_font_destroy(font);
    return out;
}

// Caracteres (no bytes) de un texto UTF-8: len() de Python.
double char_count(std::string_view text) {
    return static_cast<double>(std::ranges::count_if(text, [](char c) {
        return (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    }));
}

// --- Estilo ----------------------------------------------------------------------------------

const Json& field(const Json& obj, std::string_view key) {
    static const Json kNull;
    if (!obj.is_object()) {
        return kNull;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? kNull : *it;
}

bool has(const Json& obj, std::string_view key) {
    return obj.is_object() && obj.contains(key);
}

bool truthy(const Json& v) {
    if (v.is_null()) {
        return false;
    }
    if (v.is_boolean()) {
        return v.get<bool>();
    }
    if (v.is_number()) {
        return v.get<double>() != 0;
    }
    return !v.empty();
}

bool is_text(const Json& v, std::string_view s) {
    return v.is_string() && v.get_ref<const std::string&>() == s;
}

// float(x) de Python para un número, un bool o un texto numérico.
std::optional<double> py_float(const Json& v) {
    if (v.is_number()) {
        return v.get<double>();
    }
    if (v.is_boolean()) {
        return v.get<bool>() ? 1.0 : 0.0;
    }
    if (v.is_string()) {
        const std::string& s = v.get_ref<const std::string&>();
        char* end = nullptr;
        const double d = std::strtod(s.c_str(), &end);
        if (end != s.c_str() && *end == '\0') {
            return d;
        }
    }
    return std::nullopt;
}

// int(x) de Python: enteros, bool, reales (truncando) y texto entero.
std::optional<std::int64_t> py_int(const Json& v) {
    if (v.is_number_integer()) {
        return v.get<std::int64_t>();
    }
    if (v.is_boolean()) {
        return v.get<bool>() ? 1 : 0;
    }
    if (v.is_number_float()) {
        const double d = v.get<double>();
        return std::isfinite(d) && std::abs(d) < 9e18 ? std::optional<std::int64_t>(static_cast<std::int64_t>(d))
                                                      : std::nullopt;
    }
    if (v.is_string()) {
        const std::string& s = v.get_ref<const std::string&>();
        char* end = nullptr;
        const long long i = std::strtoll(s.c_str(), &end, 10);
        if (end != s.c_str() && *end == '\0') {
            return i;
        }
    }
    return std::nullopt;
}

// _fmt de text_ass.py: el número con 2 decimales que lee libass.
double fmt2(double v) {
    return std::strtod(std::format("{:.2f}", v).c_str(), nullptr);
}

double py_min(double a, double b) {
    return b < a ? b : a;
}
double py_max(double a, double b) {
    return b > a ? b : a;
}

std::string font_of(const Json& st) {
    const Json& f = field(st, "font");
    return f.is_string() && !f.get_ref<const std::string&>().empty() ? f.get<std::string>() : std::string("Arial");
}
bool bold_of(const Json& st) {
    return !has(st, "bold") || truthy(field(st, "bold"));
}

// "&HAABBGGRR&" de ass_bgr → color de Skia (sin alfa). nullopt si no son cifras hexadecimales.
std::optional<SkColor> ass_color(const std::string& ass) {
    if (ass.size() != 11 || ass.compare(0, 2, "&H") != 0) { // &H + AA + BBGGRR + &
        return std::nullopt;
    }
    unsigned v[3] = {};
    for (int i = 0; i < 3; ++i) {
        const std::string hex = ass.substr(4 + 2 * static_cast<std::size_t>(i), 2);
        char* end = nullptr;
        v[i] = static_cast<unsigned>(std::strtoul(hex.c_str(), &end, 16));
        if (end != hex.c_str() + 2) {
            return std::nullopt;
        }
    }
    return SkColorSetRGB(v[2], v[1], v[0]); // BB GG RR
}

// mult_alpha de libass: dos transparencias (0 opaco, 255 transparente) combinadas.
int mult_alpha(int a, int b) {
    return a - ((a * b + 0x7F) / 0xFF) + b;
}

// Fuentes con negrita propia o ya gruesas: con ellas libass no engorda la letra.
bool real_bold(std::string_view font) {
    static const std::set<std::string_view> kBold{"Arial",        "Georgia",  "Verdana", "Times New Roman",
                                                  "Courier New",  "Trebuchet MS", "Segoe UI", "Calibri",
                                                  "Tahoma",       "Consolas", "Arial Black", "Segoe UI Black"};
    return kBold.contains(font);
}

// Lo que libass necesita de un texto quieto (_style_line y los overrides de cada palabra).
struct TextParams {
    std::shared_ptr<FontFace> face;
    double em = 0;      // px por em con los que se dibuja
    double asc = 0;     // px
    double desc = 0;    // px
    double spacing = 0; // \fsp, px tras cada glifo
    double border = 0;  // Outline, px
    SkColor fill = SK_ColorWHITE;
    SkColor highlight = SK_ColorWHITE;
    SkColor outline = SK_ColorBLACK;
    int alpha_active = 0; // transparencia ASS de la palabra activa
    int alpha_idle = 0;
};

int hex_alpha(const std::string& aa) {
    return static_cast<int>(std::strtol(aa.c_str(), nullptr, 16));
}

} // namespace

// --- TextFonts -------------------------------------------------------------------------------

struct TextFonts::Impl {
    core::FontDirs dirs;
    sk_sp<SkFontMgr> mgr;
    std::mutex mutex;
    std::map<std::filesystem::path, std::shared_ptr<FontFace>> faces;

    std::shared_ptr<FontFace> face(std::string_view name, bool bold) {
        const auto path = core::resolve_font_path(name.empty() ? "Arial" : name, bold, dirs);
        const std::scoped_lock lock(mutex);
        if (const auto it = faces.find(path); it != faces.end()) {
            return it->second;
        }
        auto ff = mgr ? load_face(*mgr, path) : nullptr;
        faces.emplace(path, ff);
        return ff;
    }

    double width(std::string_view font, bool bold, double em, std::string_view text, double ls) {
        const double n = char_count(text);
        const auto ff = face(font, bold);
        if (!ff) {
            return n * em * 0.55 + ls * n;
        }
        // Pillow (layout básico de FreeType con hinting) suma avances enteros, los del hinting; con
        // la 'kern' de estas fuentes apenas mueve 1/64 de px. Aquí: cada avance redondeado y sin
        // kerning («AVAVAVAV WAWAWA» en Arial a 46 px: 483 frente a los 482,5 de Pillow).
        const double size = std::max(1.0, std::nearbyint(em * 4) / 4); // round(em · 4) / 4 de Python
        const Shaped sh = shape(*ff, text, size, 0.0, false);
        double width = 0;
        for (const double a : sh.advances) {
            width += std::round(a);
        }
        return width + ls * n;
    }
};

struct TextFontsAccess {
    static TextFonts::Impl& impl(const TextFonts& f) { return *f.impl_; }
};

TextFonts::TextFonts(core::FontDirs dirs) : impl_(std::make_shared<Impl>()) {
    impl_->dirs = std::move(dirs);
#ifdef _WIN32
    impl_->mgr = SkFontMgr_New_DirectWrite();
#endif
}
TextFonts::TextFonts(TextFonts&&) noexcept = default;
TextFonts& TextFonts::operator=(TextFonts&&) noexcept = default;
TextFonts::~TextFonts() = default;

const core::FontDirs& TextFonts::dirs() const noexcept {
    return impl_->dirs;
}

double TextFonts::text_width(std::string_view font, bool bold, double em, std::string_view text,
                             double letter_spacing) const {
    return impl_->width(font, bold, em, text, letter_spacing);
}

void TextFonts::install_text_width() const {
    core::set_text_width([impl = impl_](std::string_view font, bool bold, double em, std::string_view text,
                                        double ls) { return impl->width(font, bold, em, text, ls); });
}

// --- Dibujo ----------------------------------------------------------------------------------

namespace {

// Glifos de una palabra (lo que libass junta en un mismo mapa de bits) con sus colores.
struct Run {
    std::vector<SkGlyphID> ids;
    std::vector<SkPoint> pos;
    SkColor fill = SK_ColorWHITE;
    SkColor outline = SK_ColorBLACK;
    int alpha = 0; // transparencia ASS final (con el fundido)
};

struct Mask {
    SkBitmap bitmap;
    int left = 0;
    int top = 0;
};

// Máscara A8 de los glifos: el relleno o, con `border`, la letra engordada `border` px (el mapa
// de bits del contorno de libass, que incluye el interior).
std::optional<Mask> glyph_mask(const SkFont& font, const Run& run, double border, const SkIRect& area) {
    std::vector<SkRect> bounds(run.ids.size());
    font.getBounds(run.ids, bounds, nullptr);
    SkRect u = SkRect::MakeEmpty();
    for (std::size_t i = 0; i < bounds.size(); ++i) {
        if (!bounds[i].isEmpty()) {
            u.join(bounds[i].makeOffset(run.pos[i]));
        }
    }
    if (u.isEmpty()) {
        return std::nullopt;
    }
    u.outset(static_cast<float>(border) + 2, static_cast<float>(border) + 2);
    SkIRect ir = u.roundOut();
    if (!ir.intersect(area)) {
        return std::nullopt;
    }
    Mask m;
    m.left = ir.left();
    m.top = ir.top();
    m.bitmap.allocPixels(SkImageInfo::MakeA8(ir.width(), ir.height()));
    m.bitmap.eraseColor(SK_ColorTRANSPARENT);
    SkCanvas c(m.bitmap);
    c.translate(static_cast<float>(-ir.left()), static_cast<float>(-ir.top()));
    SkPaint p;
    p.setAntiAlias(true);
    if (border > 0) {
        p.setStyle(SkPaint::kStrokeAndFill_Style);
        p.setStrokeWidth(static_cast<float>(2 * border));
        p.setStrokeJoin(SkPaint::kRound_Join);
    }
    // El contorno de cada glifo como ruta (como lo rasteriza libass): drawGlyphs pasaría por la
    // caché de glifos de Skia, que retoca el antialias de la letra (gamma y contraste).
    for (std::size_t i = 0; i < run.ids.size(); ++i) {
        if (const auto path = font.getPath(run.ids[i])) {
            c.save();
            c.translate(run.pos[i].x(), run.pos[i].y());
            c.drawPath(*path, p);
            c.restore();
        }
    }
    return m;
}

// ass_fix_outline de libass: donde hay relleno, el contorno se aclara (o > g ? o − g/2 : 0) para
// que no se vea a través de un relleno semitransparente.
void fix_outline(const Mask& g, Mask& o) {
    const int l = std::max(g.left, o.left);
    const int t = std::max(g.top, o.top);
    const int r = std::min(g.left + g.bitmap.width(), o.left + o.bitmap.width());
    const int b = std::min(g.top + g.bitmap.height(), o.top + o.bitmap.height());
    for (int y = t; y < b; ++y) {
        const auto* gp = g.bitmap.getAddr8(l - g.left, y - g.top);
        auto* op = o.bitmap.getAddr8(l - o.left, y - o.top);
        for (int x = 0; x < r - l; ++x) {
            op[x] = op[x] > gp[x] ? static_cast<std::uint8_t>(op[x] - gp[x] / 2) : 0;
        }
    }
}

void paint_mask(SkCanvas* canvas, const Mask& m, SkColor rgb, int ass_alpha) {
    SkPaint p;
    p.setColor(SkColorSetA(rgb, static_cast<U8CPU>(255 - std::clamp(ass_alpha, 0, 255))));
    canvas->drawImage(SkImages::RasterFromBitmap(m.bitmap), static_cast<float>(m.left), static_cast<float>(m.top),
                      SkSamplingOptions(), &p);
}

// Un evento de libass (una línea): todos los contornos y después todos los rellenos.
void draw_line(SkCanvas* canvas, const SkFont& font, const std::vector<Run>& runs, double border,
               const SkIRect& area) {
    std::vector<std::optional<Mask>> fills;
    std::vector<std::optional<Mask>> outlines;
    for (const Run& r : runs) {
        fills.push_back(glyph_mask(font, r, 0.0, area));
        outlines.push_back(border > 0 ? glyph_mask(font, r, border, area) : std::nullopt);
        if (fills.back() && outlines.back()) {
            fix_outline(*fills.back(), *outlines.back());
        }
    }
    for (std::size_t i = 0; i < runs.size(); ++i) {
        if (outlines[i]) {
            paint_mask(canvas, *outlines[i], runs[i].outline, runs[i].alpha);
        }
    }
    for (std::size_t i = 0; i < runs.size(); ++i) {
        if (fills[i]) {
            paint_mask(canvas, *fills[i], runs[i].fill, runs[i].alpha);
        }
    }
}

// Por qué aún no se dibuja este texto quieto con su estilo con la pose (nullopt = se dibuja).
std::optional<std::string> static_reason(const Json& st, double height) {
    if (core::box_color(st)) {
        return "Fondo";
    }
    if (core::text_shadow(st, core::text_font_px(st, height))) {
        return "sombra o Brillo";
    }
    if (core::curve_of(st) != 0) {
        return "Curva";
    }
    const auto rot = py_float(field(st, "rotation"));
    if ((rot && std::abs(*rot) > 0.05) || truthy(field(st, "flip_h")) || truthy(field(st, "flip_v"))) {
        return "giro o volteo";
    }
    if (core::stretch_of(st) != std::pair{1.0, 1.0}) {
        return "Escala X / Y";
    }
    const core::WordFx fx = core::word_fx_set(st);
    if (fx.glow || fx.pop) {
        return "efecto de palabra brillo o pop";
    }
    const Json& appear = field(st, "block_appear");
    if (truthy(appear) && !is_text(appear, "none") && !is_text(appear, "fade") && !is_text(appear, "typing")) {
        return "aparición " + appear.dump();
    }
    if (truthy(field(st, "italic")) || truthy(field(st, "underline"))) {
        return "cursiva o subrayado";
    }
    if (bold_of(st) && !real_bold(font_of(st))) {
        return "negrita sintética de libass (" + font_of(st) + ")";
    }
    return std::nullopt;
}

Result<TextParams> text_params(const Json& st, double height, const TextFonts& text_fonts) {
    auto& fonts = TextFontsAccess::impl(text_fonts);
    TextParams tp;
    // _style_line: em = float(size) · H · escala; Fontsize = max(8, int(round(em · factor)))
    const auto size = py_float(has(st, "size") ? field(st, "size") : Json(0.048));
    const std::string font = font_of(st);
    const bool bold = bold_of(st);
    const double em_px = size.value_or(NAN) * height * core::text_style_scale(st);
    const double factor = core::ass_size_factor(font, bold, fonts.dirs);
    const double fs = std::nearbyint(em_px * factor);
    if (!std::isfinite(fs)) {
        return fail(ErrorCode::Unsupported, "tamaño de letra no válido");
    }
    const double fontsize = std::max(8.0, fs);
    tp.face = fonts.face(font, bold);
    if (!tp.face) {
        return fail(ErrorCode::Unsupported, "no se puede leer la fuente " + font);
    }
    // libass: el alto de línea OS/2 mide Fontsize
    tp.em = fontsize * tp.face->upem / (tp.face->asc + tp.face->desc);
    tp.asc = tp.face->asc * tp.em / tp.face->upem;
    tp.desc = tp.face->desc * tp.em / tp.face->upem;
    tp.spacing = fmt2(core::text_letter_spacing_px(st, em_px));
    const Json& bw = field(st, "border_width");
    const auto border = truthy(bw) ? py_int(bw) : std::optional<std::int64_t>(0);
    if (!border) {
        return fail(ErrorCode::Unsupported, "grosor del trazo no válido");
    }
    tp.border = static_cast<double>(std::max<std::int64_t>(0, *border));
    const auto fill = ass_color(core::ass_bgr(core::style_color(st, "color", "#ffffff")));
    const auto hi = ass_color(core::ass_bgr(core::style_color(st, "highlight_color", "#ffffff")));
    const auto outline = ass_color(core::ass_bgr(core::style_color(st, "border_color", "#000000")));
    if (!fill || !hi || !outline) {
        return fail(ErrorCode::Unsupported, "color no válido");
    }
    tp.fill = *fill;
    tp.highlight = *hi;
    tp.outline = *outline;
    tp.alpha_active = hex_alpha(core::alpha_hex(core::word_opacity(st, true)));
    tp.alpha_idle = hex_alpha(core::alpha_hex(core::word_opacity(st, false)));
    return tp;
}

// \fad(180,0) del primer evento (interpolate_alpha de libass), en el instante `rel` del evento.
int fade_alpha(std::int64_t rel) {
    if (rel < 0) {
        return 255;
    }
    if (rel < 180) {
        const double cf = static_cast<double>(rel) / 180.0;
        return static_cast<int>(255 * (1 - cf));
    }
    return 0;
}

void draw_event(SkCanvas* canvas, const TextParams& tp, const Json& st, const core::TextEvent& e, std::int64_t now,
                double width, double height) {
    SkFont font(tp.face->typeface, static_cast<float>(tp.em));
    font.setHinting(SkFontHinting::kNone);
    font.setSubpixel(true);
    font.setLinearMetrics(true);
    font.setEdging(SkFont::Edging::kAntiAlias);
    const int fade = e.first && is_text(field(st, "block_appear"), "fade") ? fade_alpha(now - e.start_ms) : 0;

    // _layout_prefix: \pos de cada línea
    const auto [px, py] = core::text_xy(st, width, height);
    const auto wbox = py_float(has(st, "w") ? field(st, "w") : Json(0.88)).value_or(0.88);
    const double half = py_max(0.2, py_min(1.0, wbox)) * width / 2.0 * core::text_style_scale(st);
    const double font_px = core::text_font_px(st, height);
    const double inset = 0.2 * font_px;
    const double lh = font_px * core::text_line_height(st);
    const Json& align = field(st, "align");
    const auto rows = core::text_rows(st, width, height, e.content.text);
    const SkIRect area = SkIRect::MakeWH(static_cast<int>(width), static_cast<int>(height));

    std::int64_t gi = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].empty()) {
            continue;
        }
        std::string line;
        std::vector<std::size_t> word_end; // byte donde acaba cada palabra (con su espacio)
        for (std::size_t w = 0; w < rows[i].size(); ++w) {
            line += (w == 0 ? "" : " ") + rows[i][w];
            word_end.push_back(line.size() + 1);
        }
        const double row_dy = (static_cast<double>(i) - static_cast<double>(rows.size() - 1) / 2.0) * lh;
        const double ax = is_text(align, "left")    ? static_cast<double>(px) - half + inset
                          : is_text(align, "right") ? static_cast<double>(px) + half - inset
                                                    : static_cast<double>(px);
        const double pos_x = fmt2(ax);
        const double pos_y = fmt2(static_cast<double>(py) + row_dy);
        const Shaped sh = shape(*tp.face, line, tp.em, tp.spacing, false);
        const double left = is_text(align, "left")    ? pos_x
                            : is_text(align, "right") ? pos_x - sh.width
                                                      : pos_x - sh.width / 2;
        const double baseline = pos_y + tp.asc - (tp.asc + tp.desc) / 2;

        std::vector<Run> runs(rows[i].size());
        for (std::size_t w = 0; w < runs.size(); ++w) {
            const bool active = e.content.is_active(gi + static_cast<std::int64_t>(w));
            runs[w].fill = active ? tp.highlight : tp.fill;
            runs[w].outline = tp.outline;
            runs[w].alpha = mult_alpha(active ? tp.alpha_active : tp.alpha_idle, fade);
        }
        for (std::size_t g = 0; g < sh.ids.size(); ++g) {
            std::size_t w = 0;
            while (w + 1 < word_end.size() && sh.clusters[g] >= word_end[w]) {
                ++w;
            }
            runs[w].ids.push_back(sh.ids[g]);
            runs[w].pos.push_back({static_cast<float>(left + sh.x[g]), static_cast<float>(baseline + sh.y[g])});
        }
        draw_line(canvas, font, runs, tp.border, area);
        gi += static_cast<std::int64_t>(rows[i].size());
    }
}

const core::TimelineTrack* find_track(const core::Timeline& tl, const std::string& id) {
    for (const auto& t : tl.tracks) {
        if (t.id == id) {
            return &t;
        }
    }
    return nullptr;
}

} // namespace

Result<void> draw_timeline_texts(Canvas& canvas, const core::Timeline& timeline, std::int64_t k,
                                 const TextFonts& fonts, bool omit, std::vector<std::string>* warnings) {
    fonts.install_text_width();
    SkCanvas* sk = CanvasAccess::sk(canvas);
    const double width = static_cast<double>(timeline.width - timeline.width % 2);
    const double height = static_cast<double>(timeline.height - timeline.height % 2);
    const double fps = timeline.fps > 0 ? static_cast<double>(timeline.fps) : 30.0;
    const std::int64_t now = core::ass_frame_ms(k, fps);
    const double t = static_cast<double>(k) / fps;
    const auto buried = core::buried_text_track_ids(timeline.tracks);

    for (const auto& cow : timeline.clips) {
        const core::TimelineClip& clip = *cow;
        if (clip.kind != "text" || !core::text_in_ass(clip)) {
            continue;
        }
        const core::TimelineTrack* track = find_track(timeline, clip.track_id);
        if (track == nullptr || track->hidden) {
            continue;
        }
        core::TimelineClip c = clip;
        const Json base = core::effective_text_style(track->style.value_or(Json()), clip.style.value_or(Json()));
        if (has(base, "text_case") && truthy(base.at("text_case"))) {
            c.text = core::apply_text_case(c.text.value_or(""), base.at("text_case"));
        }
        const Json st = core::posed_text_style(c, base, 0.0);
        std::optional<std::string> reason;
        std::vector<core::TextEvent> visible;
        const double start = std::max(0.0, c.start);
        if (core::text_animates(c, fps)) {
            if (t >= start && t < start + core::text_clip_duration(c)) {
                reason = "texto animado";
            }
        } else {
            for (auto& e : core::static_text_events(c, st)) {
                if (e.start_ms <= now && now < e.end_ms) {
                    visible.push_back(std::move(e));
                }
            }
            if (visible.empty()) {
                continue;
            }
            const core::TextPose pose = core::text_pose_at(c, 0.0);
            if (buried.contains(c.track_id)) {
                reason = "pista de texto con vídeo encima";
            } else if (core::has_mask(c) || core::clip_blend(c) != "normal" || std::abs(pose.rot_x) >= 0.05 ||
                       std::abs(pose.rot_y) >= 0.05) {
                reason = "máscara, fusión o giro 3D";
            } else {
                reason = static_reason(st, height);
            }
        }
        if (!reason && visible.empty()) {
            continue;
        }
        Result<TextParams> tp = reason ? Result<TextParams>(fail(ErrorCode::Unsupported, *reason))
                                       : text_params(st, height, fonts);
        if (!tp) {
            const std::string msg = std::format("texto {}: {} aún no se dibuja", c.id, tp.error().message);
            if (!omit) {
                return fail(ErrorCode::Unsupported, msg);
            }
            if (warnings != nullptr) {
                warnings->push_back(msg);
            }
            continue;
        }
        for (const auto& e : visible) {
            draw_event(sk, *tp, st, e, now, width, height);
        }
    }
    return {};
}

} // namespace gc::render
