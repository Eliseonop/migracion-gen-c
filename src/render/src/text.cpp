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
#include <tuple>
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
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkTypeface.h"
#include "include/effects/SkImageFilters.h"

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
    std::vector<double> x_offsets;       // desplazamiento horizontal de HarfBuzz de cada glifo
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
        out.x_offsets.push_back(pos[i].x_offset / 64.0);
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
    double border = 0;  // Outline, px (entero, el de _style_line)
    double border_float = 0; // float(border_width): el \bord del evento de la sombra
    double size_factor = 1;  // ass_size_factor de la fuente (alto de la caja de Fondo dibujada)
    SkColor fill = SK_ColorWHITE;
    SkColor highlight = SK_ColorWHITE;
    SkColor outline = SK_ColorBLACK;
    int alpha_active = 0; // transparencia ASS de la palabra activa
    int alpha_idle = 0;
    std::optional<SkColor> box; // color del Fondo (sin alfa), si hay
    int box_alpha = 0;
    bool box_drawn = false;     // Fondo como forma (box_rects) en vez de BorderStyle 4
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

// σ de la gaussiana de \blurN en libass, en px por unidad de N: 0,85, el LIBASS_BLUR_SIGMA de
// text_ass.py. Con la fixture del Brillo es el mejor (0,69: 40,6 dB perdonando 1 px; 0,85: 43,0;
// 0,95: 40,3); medir la pendiente de un borde recto lo subestima (sale 0,69).
constexpr double kBlurSigma = 0.85;

// Glifos de una palabra (lo que libass junta en un mismo mapa de bits) con sus colores, su escala
// (\fscx, \fscy) y su desenfoque (\blur).
struct Run {
    std::vector<SkGlyphID> ids;
    std::vector<SkPoint> pos; // origen de cada glifo
    float sx = 1;
    float sy = 1;
    SkColor fill = SK_ColorWHITE;
    SkColor outline = SK_ColorBLACK;
    int alpha = 0;   // transparencia ASS final (con el fundido)
    double blur = 0; // \blur N
};

struct Mask {
    SkBitmap bitmap;
    int left = 0;
    int top = 0;
};

// Contorno de un glifo de la palabra, escalado y en su sitio.
std::optional<SkPath> glyph_path(const SkFont& font, const Run& run, std::size_t i) {
    auto path = font.getPath(run.ids[i]);
    if (!path) {
        return std::nullopt;
    }
    return path->makeTransform(SkMatrix::Translate(run.pos[i].x(), run.pos[i].y()) * SkMatrix::Scale(run.sx, run.sy));
}

// Máscara A8 de los glifos: el relleno o, con `border`, la letra engordada `border` px (el mapa
// de bits del contorno de libass, que incluye el interior). Los contornos van como rutas (como los
// rasteriza libass): drawGlyphs pasaría por la caché de Skia, que retoca el antialias.
std::optional<Mask> glyph_mask(const SkFont& font, const Run& run, double border, const SkIRect& area) {
    std::vector<SkPath> paths;
    SkRect u = SkRect::MakeEmpty();
    for (std::size_t i = 0; i < run.ids.size(); ++i) {
        if (auto p = glyph_path(font, run, i)) {
            u.join(p->getBounds());
            paths.push_back(std::move(*p));
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
    for (const auto& path : paths) {
        c.drawPath(path, p);
    }
    return m;
}

// La máscara desenfocada con la gaussiana de \blurN (con margen para el halo).
Mask blurred(const Mask& m, double n) {
    const double sigma = kBlurSigma * n;
    const int margin = static_cast<int>(std::ceil(3 * sigma)) + 1;
    Mask out;
    out.left = m.left - margin;
    out.top = m.top - margin;
    out.bitmap.allocPixels(SkImageInfo::MakeA8(m.bitmap.width() + 2 * margin, m.bitmap.height() + 2 * margin));
    out.bitmap.eraseColor(SK_ColorTRANSPARENT);
    SkCanvas c(out.bitmap);
    SkPaint p;
    p.setImageFilter(SkImageFilters::Blur(static_cast<float>(sigma), static_cast<float>(sigma), nullptr));
    c.drawImage(SkImages::RasterFromBitmap(m.bitmap), static_cast<float>(margin), static_cast<float>(margin),
                SkSamplingOptions(), &p);
    return out;
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

SkColor with_alpha(SkColor rgb, int ass_alpha) {
    return SkColorSetA(rgb, static_cast<U8CPU>(255 - std::clamp(ass_alpha, 0, 255)));
}

void paint_mask(SkCanvas* canvas, const Mask& m, SkColor rgb, int ass_alpha) {
    SkPaint p;
    p.setColor(with_alpha(rgb, ass_alpha));
    canvas->drawImage(SkImages::RasterFromBitmap(m.bitmap), static_cast<float>(m.left), static_cast<float>(m.top),
                      SkSamplingOptions(), &p);
}

// Caja de BorderStyle 4 (Fondo por línea): detrás del contorno y del relleno.
struct LineBox {
    SkRect rect;
    SkColor rgb = SK_ColorBLACK;
    int alpha = 0;
};

// Un evento de libass (una línea): la caja, todos los contornos y después todos los rellenos. Con
// \blur se desenfoca el contorno si hay trazo; si no, el relleno.
void draw_line(SkCanvas* canvas, const SkFont& font, const std::vector<Run>& runs, double border,
               const std::optional<LineBox>& box, const SkIRect& area) {
    std::vector<std::optional<Mask>> fills;
    std::vector<std::optional<Mask>> outlines;
    for (const Run& r : runs) {
        fills.push_back(glyph_mask(font, r, 0.0, area));
        outlines.push_back(border > 0 ? glyph_mask(font, r, border, area) : std::nullopt);
        if (r.blur > 0) {
            auto& target = outlines.back() ? outlines.back() : fills.back();
            if (target) {
                target = blurred(*target, r.blur);
            }
        }
        if (fills.back() && outlines.back()) {
            fix_outline(*fills.back(), *outlines.back());
        }
    }
    if (box) {
        SkPaint p;
        p.setAntiAlias(true);
        p.setColor(with_alpha(box->rgb, box->alpha));
        canvas->drawRect(box->rect, p);
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
std::optional<std::string> static_reason(const Json& st) {
    if (core::curve_of(st) != 0) {
        return "Curva";
    }
    const auto rot = py_float(field(st, "rotation"));
    if ((rot && std::abs(*rot) > 0.05) || truthy(field(st, "flip_h")) || truthy(field(st, "flip_v"))) {
        return "giro o volteo";
    }
    const Json& appear = field(st, "block_appear");
    if (truthy(appear) && !is_text(appear, "none") && !is_text(appear, "fade") && !is_text(appear, "typing") &&
        !is_text(appear, "pop") && !is_text(appear, "slide_up")) {
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
    tp.size_factor = core::ass_size_factor(font, bold, fonts.dirs);
    const double fs = std::nearbyint(em_px * tp.size_factor);
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
    // El evento de la sombra lleva \bord{float(border_width)} (el del estilo es entero).
    tp.border_float = truthy(bw) ? py_float(bw).value_or(0.0) : 0.0;
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
    if (const auto box = core::box_color(st)) {
        const auto rgb = ass_color(*box);
        if (!rgb) {
            return fail(ErrorCode::Unsupported, "color del Fondo no válido");
        }
        tp.box = *rgb;
        tp.box_alpha = hex_alpha(box->substr(2, 2));
        tp.box_drawn = core::box_drawn(st);
    }
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

// Avance de \move y de \t(0,180,…) en el instante `rel` del evento: 0 a 1 en 180 ms.
double appear_progress(std::int64_t rel) {
    return std::clamp(static_cast<double>(rel) / 180.0, 0.0, 1.0);
}

// Lo común a los tres tipos de evento de un texto en un instante: el estilo, la geometría de
// _layout_prefix y la aparición de bloque del primer evento.
struct EventFrame {
    const TextParams* tp = nullptr;
    const Json* st = nullptr;
    double width = 0;
    double height = 0;
    std::int64_t px = 0;
    std::int64_t py = 0;
    double half = 0;
    double inset = 0;
    double lh = 0;
    double sx = 1;
    double sy = 1;
    int fade = 0;           // transparencia del \fad
    double slide = 0;       // px que aún baja la línea (\move de slide_up), sin _fmt
    bool sliding = false;
    double pop_scale = 1;   // \fscx de la aparición pop (solo lo nota la caja dibujada)
    bool popping = false;
};

EventFrame event_frame(const TextParams& tp, const Json& st, const core::TextEvent& e, std::int64_t now,
                       double width, double height) {
    EventFrame f;
    f.tp = &tp;
    f.st = &st;
    f.width = width;
    f.height = height;
    std::tie(f.px, f.py) = core::text_xy(st, width, height);
    const auto wbox = py_float(has(st, "w") ? field(st, "w") : Json(0.88)).value_or(0.88);
    f.half = py_max(0.2, py_min(1.0, wbox)) * width / 2.0 * core::text_style_scale(st);
    const double font_px = core::text_font_px(st, height);
    f.inset = 0.2 * font_px;
    f.lh = font_px * core::text_line_height(st);
    std::tie(f.sx, f.sy) = core::stretch_of(st);
    const Json& appear = field(st, "block_appear");
    const std::int64_t rel = now - e.start_ms;
    if (e.first && is_text(appear, "fade")) {
        f.fade = fade_alpha(rel);
    }
    if (e.first && is_text(appear, "slide_up")) {
        f.sliding = true;
        f.slide = std::max(12.0, std::nearbyint(height * 0.035)) * (1.0 - appear_progress(rel));
    }
    if (e.first && is_text(appear, "pop")) {
        f.popping = true;
        f.pop_scale = 0.8 + 0.2 * appear_progress(rel);
    }
    return f;
}

// \pos de una línea (o su \move en el instante): _layout_prefix con `off` (la sombra) y `row_dy`.
SkPoint line_pos(const EventFrame& f, double ax_raw, double row_dy, double off_x, double off_y) {
    const double ax = static_cast<double>(f.px) + (ax_raw - static_cast<double>(f.px)) * f.sx + off_x;
    const double ry = static_cast<double>(f.py) + off_y + row_dy * f.sy;
    double y = fmt2(ry);
    if (f.sliding) {
        // \move(ax, ry + dy, ax, ry, 0, 180): de fmt(ry + dy) a fmt(ry)
        const double dy = std::max(12.0, std::nearbyint(f.height * 0.035));
        const double y0 = fmt2(ry + dy);
        y = y0 + (fmt2(ry) - y0) * (dy == 0 ? 1.0 : 1.0 - f.slide / dy);
    }
    return {static_cast<float>(fmt2(ax)), static_cast<float>(y)};
}

double anchor_x(const EventFrame& f) {
    const Json& align = field(*f.st, "align");
    return is_text(align, "left")    ? static_cast<double>(f.px) - f.half + f.inset
           : is_text(align, "right") ? static_cast<double>(f.px) + f.half - f.inset
                                     : static_cast<double>(f.px);
}

// Una línea colocada: los glifos de cada palabra con su escala y la caja de la línea (avance ×
// asc + desc de la palabra más alta).
struct PlacedLine {
    std::vector<Run> runs;
    SkRect box;
};

PlacedLine place_line(const EventFrame& f, const std::vector<std::string>& words, SkPoint pos,
                      const std::vector<std::pair<float, float>>& scales) {
    const TextParams& tp = *f.tp;
    std::string line;
    std::vector<std::size_t> word_end; // byte donde acaba cada palabra (con su espacio)
    for (std::size_t w = 0; w < words.size(); ++w) {
        line += (w == 0 ? "" : " ") + words[w];
        word_end.push_back(line.size() + 1);
    }
    const Shaped sh = shape(*tp.face, line, tp.em, 0.0, false);
    PlacedLine out;
    out.runs.resize(words.size());
    std::vector<std::size_t> owner(sh.ids.size());
    std::vector<double> x(sh.ids.size());
    double pen = 0;
    for (std::size_t g = 0; g < sh.ids.size(); ++g) {
        std::size_t w = 0;
        while (w + 1 < word_end.size() && sh.clusters[g] >= word_end[w]) {
            ++w;
        }
        owner[g] = w;
        const double s = scales[w].first;
        x[g] = pen + sh.x_offsets[g] * s;
        pen += (sh.advances[g] + tp.spacing) * s; // libass escala también el \fsp con \fscx
    }
    double asc = 0;
    double desc = 0;
    for (const auto& [s_x, s_y] : scales) {
        asc = std::max(asc, tp.asc * s_y);
        desc = std::max(desc, tp.desc * s_y);
    }
    const Json& align = field(*f.st, "align");
    const double left = is_text(align, "left")    ? pos.x()
                        : is_text(align, "right") ? pos.x() - pen
                                                  : pos.x() - pen / 2;
    const double baseline = pos.y() + asc - (asc + desc) / 2;
    for (std::size_t g = 0; g < sh.ids.size(); ++g) {
        const std::size_t w = owner[g];
        const auto [s_x, s_y] = scales[w];
        out.runs[w].ids.push_back(sh.ids[g]);
        out.runs[w].pos.push_back({static_cast<float>(left + x[g]), static_cast<float>(baseline + sh.y[g] * s_y)});
        out.runs[w].sx = s_x;
        out.runs[w].sy = s_y;
    }
    out.box = SkRect::MakeLTRB(static_cast<float>(left), static_cast<float>(baseline - asc),
                               static_cast<float>(left + pen), static_cast<float>(baseline + desc));
    return out;
}

SkFont make_font(const TextParams& tp) {
    SkFont font(tp.face->typeface, static_cast<float>(tp.em));
    font.setHinting(SkFontHinting::kNone);
    font.setSubpixel(true);
    font.setLinearMetrics(true);
    font.setEdging(SkFont::Edging::kAntiAlias);
    return font;
}

// Evento del texto (capa 2i+1): por línea, la caja de BorderStyle 4 si hay Fondo sin dibujar, y
// cada palabra con su color, su opacidad, el pop (\fscx118) o el brillo (\blur3) de la activa.
void draw_text_event(SkCanvas* canvas, const EventFrame& f, const core::TextEvent& e) {
    const TextParams& tp = *f.tp;
    const SkFont font = make_font(tp);
    const core::WordFx fx = core::word_fx_set(*f.st);
    const auto rows = core::text_rows(*f.st, f.width, f.height, e.content.text);
    const SkIRect area = SkIRect::MakeWH(static_cast<int>(f.width), static_cast<int>(f.height));
    std::int64_t gi = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].empty()) {
            continue;
        }
        const double row_dy = (static_cast<double>(i) - static_cast<double>(rows.size() - 1) / 2.0) * f.lh;
        std::vector<std::pair<float, float>> scales;
        std::vector<bool> active;
        for (std::size_t w = 0; w < rows[i].size(); ++w) {
            active.push_back(e.content.is_active(gi + static_cast<std::int64_t>(w)));
            const double s = active.back() && fx.pop ? 1.18 : 1.0; // _fsc(st, 118) de la activa con pop
            scales.emplace_back(static_cast<float>(s * f.sx), static_cast<float>(s * f.sy));
        }
        PlacedLine pl = place_line(f, rows[i], line_pos(f, anchor_x(f), row_dy, 0, 0), scales);
        for (std::size_t w = 0; w < pl.runs.size(); ++w) {
            pl.runs[w].fill = active[w] ? tp.highlight : tp.fill;
            pl.runs[w].outline = tp.outline;
            pl.runs[w].alpha = mult_alpha(active[w] ? tp.alpha_active : tp.alpha_idle, f.fade);
            pl.runs[w].blur = active[w] && fx.glow ? 3.0 : 0.0;
        }
        std::optional<LineBox> box;
        if (tp.box && !tp.box_drawn) {
            box = LineBox{pl.box.makeOutset(static_cast<float>(tp.border), static_cast<float>(tp.border)), *tp.box,
                          mult_alpha(tp.box_alpha, f.fade)};
        }
        draw_line(canvas, font, pl.runs, tp.border, box, area);
        gi += static_cast<std::int64_t>(rows[i].size());
    }
}

// Evento de la sombra paralela o del halo del Brillo (capa 2i): la silueta (relleno y trazo en su
// color) desplazada, con su opacidad y \blur (shadow_dialogues y _shadow_override).
void draw_shadow_event(SkCanvas* canvas, const EventFrame& f, const core::TextShadow& sh, const std::string& text) {
    const TextParams& tp = *f.tp;
    const auto rgb = ass_color(core::ass_bgr(sh.color));
    if (!rgb) {
        return;
    }
    const SkFont font = make_font(tp);
    const int alpha = mult_alpha(hex_alpha(core::alpha_hex(sh.opacity * core::style_opacity(*f.st))), f.fade);
    const double blur = fmt2(sh.sigma / 0.85); // \blur{_fmt(σ / LIBASS_BLUR_SIGMA)}
    const double border = fmt2(tp.border_float + sh.spread);
    const auto rows = core::text_rows(*f.st, f.width, f.height, text);
    const SkIRect area = SkIRect::MakeWH(static_cast<int>(f.width), static_cast<int>(f.height));
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].empty()) {
            continue;
        }
        const double row_dy = (static_cast<double>(i) - static_cast<double>(rows.size() - 1) / 2.0) * f.lh;
        const std::vector<std::pair<float, float>> scales(
            rows[i].size(), {static_cast<float>(f.sx), static_cast<float>(f.sy)});
        PlacedLine pl = place_line(f, rows[i], line_pos(f, anchor_x(f), row_dy, sh.dx, sh.dy), scales);
        for (auto& r : pl.runs) {
            r.fill = *rgb;
            r.outline = *rgb;
            r.alpha = alpha;
            r.blur = blur;
        }
        draw_line(canvas, font, pl.runs, border, std::nullopt, area);
    }
}

// Fondo dibujado (capa 2i, box_dialogues): un rectángulo redondeado por caja de box_rects, en su
// \pos y con la aparición del texto (la pop sí escala la forma: sus etiquetas no reponen \fscx).
void draw_box_event(SkCanvas* canvas, const EventFrame& f, const std::string& text) {
    const TextParams& tp = *f.tp;
    const auto rows = core::text_rows(*f.st, f.width, f.height, text);
    const double radius = std::max(0.0, std::min(1.0, py_float(field(*f.st, "bg_radius")).value_or(0.0)));
    const double scale_x = f.popping ? f.pop_scale * f.sx : 1.0;
    const double scale_y = f.popping ? f.pop_scale * f.sy : 1.0;
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(with_alpha(*tp.box, mult_alpha(tp.box_alpha, f.fade)));
    for (const auto& b : core::box_rects(*f.st, f.width, f.height, rows, tp.size_factor)) {
        // _rrect_path(w · sx, h · sy, radio) con las coordenadas a 2 decimales (_fmt)
        const double w = fmt2(b.w * f.sx);
        const double h = fmt2(b.h * f.sy);
        const SkPoint pos = line_pos(f, static_cast<double>(f.px) + b.x, b.y, 0, 0);
        const double r = radius * std::min(w, h) / 2;
        const double k = r * (1 - 0.5523);
        SkPathBuilder pb;
        if (r <= 0) {
            pb.addRect(SkRect::MakeWH(static_cast<float>(w), static_cast<float>(h)));
        } else {
            const auto F = [](double v) { return static_cast<float>(fmt2(v)); };
            pb.moveTo(F(r), 0);
            pb.lineTo(F(w - r), 0);
            pb.cubicTo(F(w - k), 0, F(w), F(k), F(w), F(r));
            pb.lineTo(F(w), F(h - r));
            pb.cubicTo(F(w), F(h - k), F(w - k), F(h), F(w - r), F(h));
            pb.lineTo(F(r), F(h));
            pb.cubicTo(F(k), F(h), 0, F(h - k), 0, F(h - r));
            pb.lineTo(0, F(r));
            pb.cubicTo(0, F(k), F(k), 0, F(r), 0);
            pb.close();
        }
        // \an5 de un dibujo: el centro de la forma (ya escalada) en el \pos
        const SkPath path = pb.detach().makeTransform(
            SkMatrix::Translate(pos.x() - static_cast<float>(w * scale_x / 2), pos.y() - static_cast<float>(h * scale_y / 2)) *
            SkMatrix::Scale(static_cast<float>(scale_x), static_cast<float>(scale_y)));
        canvas->drawPath(path, p);
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
    const auto visible_at = [now](const core::TextEvent& e) { return e.start_ms <= now && now < e.end_ms; };

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
        const double start = std::max(0.0, c.start);
        const double dur = core::text_clip_duration(c);
        if (core::text_animates(c, fps)) {
            if (t >= start && t < start + dur) {
                const std::string msg = std::format("texto {}: texto animado aún no se dibuja", c.id);
                if (!omit) {
                    return fail(ErrorCode::Unsupported, msg);
                }
                if (warnings != nullptr) {
                    warnings->push_back(msg);
                }
            }
            continue;
        }
        // Los eventos del texto y, con sombra o Fondo dibujado, los de su capa de abajo: todo el
        // clip (o los pasos de «escribir») con el texto entero.
        const auto text_events = core::static_text_events(c, st);
        std::vector<core::TextEvent> under_events;
        if (is_text(field(st, "block_appear"), "typing")) {
            under_events = text_events;
        } else {
            under_events.push_back({core::ass_time_ms(start), core::ass_time_ms(start + std::max(0.04, dur)),
                                    core::TextContent{c.text.value_or("")}, true});
        }
        const double font_px = core::text_font_px(st, height);
        const auto shadow = core::text_shadow(st, font_px);
        const bool drawn_box = core::box_drawn(st);
        const bool any_text = std::ranges::any_of(text_events, visible_at);
        const bool any_under = (shadow || drawn_box) && std::ranges::any_of(under_events, visible_at);
        if (!any_text && !any_under) {
            continue;
        }
        std::optional<std::string> reason;
        const core::TextPose pose = core::text_pose_at(c, 0.0);
        if (buried.contains(c.track_id)) {
            reason = "pista de texto con vídeo encima";
        } else if (core::has_mask(c) || core::clip_blend(c) != "normal" || std::abs(pose.rot_x) >= 0.05 ||
                   std::abs(pose.rot_y) >= 0.05) {
            reason = "máscara, fusión o giro 3D";
        } else {
            reason = static_reason(st);
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
        // Capa 2i: el Fondo dibujado y después la sombra; capa 2i+1: el texto.
        for (const auto& e : under_events) {
            if (drawn_box && visible_at(e)) {
                draw_box_event(sk, event_frame(*tp, st, e, now, width, height), e.content.text);
            }
        }
        for (const auto& e : under_events) {
            if (shadow && visible_at(e)) {
                draw_shadow_event(sk, event_frame(*tp, st, e, now, width, height), *shadow, e.content.text);
            }
        }
        for (const auto& e : text_events) {
            if (visible_at(e)) {
                draw_text_event(sk, event_frame(*tp, st, e, now, width, height), e);
            }
        }
    }
    return {};
}

} // namespace gc::render
