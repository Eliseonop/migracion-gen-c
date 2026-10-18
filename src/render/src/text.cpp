#include "gc/render/text.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <numbers>
#include <set>
#include <tuple>
#include <utility>

#include <hb.h>

#include "gc/core/blend.hpp"
#include "gc/core/masks.hpp"
#include "gc/core/text3d.hpp"
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
#include "text_mask.hpp"
#include "py.hpp"

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
    // Los de FreeType (face->ascender y face->descender, los que mide Pillow): los de hhea; si los
    // dos son 0, los tipográficos de la OS/2 y, si no, los de Windows. El descendente, negativo.
    std::int64_t ft_asc = 0;
    std::int64_t ft_desc = 0;

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
    const int typo_asc = len >= 72 ? static_cast<std::int16_t>(be16(d + 68)) : 0;
    const int typo_desc = len >= 72 ? static_cast<std::int16_t>(be16(d + 70)) : 0;
    hb_blob_destroy(os2);
    int hhea_asc = 0;
    int hhea_desc = 0;
    hb_blob_t* hhea = hb_face_reference_table(ff->face, HB_TAG('h', 'h', 'e', 'a'));
    const char* h = hb_blob_get_data(hhea, &len);
    if (len >= 8) {
        hhea_asc = static_cast<std::int16_t>(be16(h + 4));
        hhea_desc = static_cast<std::int16_t>(be16(h + 6));
    }
    hb_blob_destroy(hhea);
    if (has_os2 && win_asc + win_desc != 0) {
        ff->asc = win_asc;
        ff->desc = win_desc;
    } else {
        ff->asc = hhea_asc;
        ff->desc = -hhea_desc;
    }
    if (hhea_asc != 0 || hhea_desc != 0) {
        ff->ft_asc = hhea_asc;
        ff->ft_desc = hhea_desc;
    } else if (typo_asc != 0 || typo_desc != 0) {
        ff->ft_asc = typo_asc;
        ff->ft_desc = typo_desc;
    } else {
        ff->ft_asc = win_asc;
        ff->ft_desc = -win_desc;
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

// float() e int() de Python, los del núcleo (espacios alrededor, «_», «inf», sin hexadecimales).
using core::detail::py_float;
using core::detail::py_int;

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

core::TextWidthFn TextFonts::width_fn() const {
    return [impl = impl_](std::string_view font, bool bold, double em, std::string_view text, double ls) {
        return impl->width(font, bold, em, text, ls);
    };
}

// --- Medidas de Pillow y máscara de tipo texto -----------------------------------------------

namespace {

// FT_DivFix y FT_MulFix de FreeType (16.16, redondeando), en enteros de 64 bits.
std::int64_t ft_divfix(std::int64_t a, std::int64_t b) {
    const bool neg = (a < 0) != (b < 0);
    const std::int64_t q = ((std::abs(a) << 16) + (std::abs(b) >> 1)) / std::abs(b);
    return neg ? -q : q;
}

std::int64_t ft_mulfix(std::int64_t a, std::int64_t b) {
    const bool neg = (a < 0) != (b < 0);
    const std::int64_t p = ((std::abs(a) * std::abs(b)) + 0x8000) >> 16;
    return neg ? -p : p;
}

// PIXEL de _imagingft.c: de 26.6 a px enteros, con las mitades hacia arriba.
std::int64_t ft_pixel(std::int64_t v) { return (v + 32) >> 6; }

// Una línea como la maquetación básica de Pillow: los glifos de HarfBuzz sin kerning y cada avance
// redondeado al px (los del hinting), con el origen de cada glifo.
struct PillowLine {
    std::vector<SkGlyphID> ids;
    std::vector<double> x;
    std::int64_t width = 0; // px
};

PillowLine pillow_line(const FontFace& ff, std::string_view text, int size) {
    const Shaped sh = shape(ff, text, size, 0.0, false);
    PillowLine out;
    double pen = 0;
    for (std::size_t i = 0; i < sh.ids.size(); ++i) {
        out.ids.push_back(sh.ids[i]);
        out.x.push_back(pen);
        pen += std::round(sh.advances[i]);
    }
    out.width = static_cast<std::int64_t>(pen);
    return out;
}

} // namespace

std::optional<PillowText> pillow_text(const TextFonts& fonts, std::string_view font, bool bold, int size,
                                      std::string_view text, std::string_view anchor) {
    const auto ff = TextFontsAccess::impl(fonts).face(font, bold);
    if (!ff || size <= 0 || anchor.size() != 2) {
        return std::nullopt;
    }
    // tt_size_reset de FreeType: ascendente al techo y descendente al suelo del px, en 26.6.
    const std::int64_t y_scale = ft_divfix(static_cast<std::int64_t>(size) * 64, static_cast<std::int64_t>(ff->upem));
    const std::int64_t asc = (ft_mulfix(ff->ft_asc, y_scale) + 63) & -64;
    const std::int64_t desc = ft_mulfix(ff->ft_desc, y_scale) & -64;
    const PillowLine line = pillow_line(*ff, text, size);
    const std::int64_t width = line.width * 64;
    PillowText out;
    out.ascent = static_cast<int>(asc / 64);
    out.descent = static_cast<int>(-desc / 64);
    out.length = static_cast<double>(line.width);
    // bounding_box_and_anchors de _imagingft.c (con la división entera de C).
    const char h = anchor[0];
    const char v = anchor[1];
    const std::int64_t x_anchor = h == 'm' ? ft_pixel(width / 2) : (h == 'r' ? ft_pixel(width) : 0);
    const std::int64_t y_anchor = v == 'm' ? ft_pixel((asc + desc) / 2) : 0;
    out.origin_x = -static_cast<double>(x_anchor);
    out.origin_y = static_cast<double>(y_anchor);
    return out;
}

bool paint_text_mask(SkCanvas& canvas, const SkPaint& paint, const Json& m, double cx, double cy, double hw,
                     double ref, const TextFonts& fonts) {
    static const Json kEmpty = Json::object();
    const Json& t = m.contains("text") && m.at("text").is_object() ? m.at("text") : kEmpty;
    const auto num = [&t](const char* key, double fallback) {
        return core::detail::py_num(t.contains(key) ? t.at(key) : Json(), fallback);
    };
    // size = max(2, int(round(size · ref))); el peso decide la negrita, como resolve_font_path.
    const auto size = static_cast<int>(std::max(2.0, std::nearbyint(num("size", 0.22) * ref)));
    const bool bold = num("weight", 700) >= 600;
    std::string font = "Anton";
    if (t.contains("font") && t.at("font").is_string() && !t.at("font").get_ref<const std::string&>().empty()) {
        font = t.at("font").get<std::string>();
    }
    const std::string align = t.contains("align") && t.at("align").is_string() ? t.at("align").get<std::string>()
                                                                                 : std::string("center");
    const std::string content =
        t.contains("content") && t.at("content").is_string() ? t.at("content").get<std::string>() : std::string();
    const auto ff = TextFontsAccess::impl(fonts).face(font, bold);
    if (!ff) {
        return false;
    }
    std::vector<std::string_view> lines;
    for (std::size_t pos = 0;;) {
        const std::size_t nl = content.find('\n', pos);
        lines.push_back(std::string_view(content).substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        if (nl == std::string::npos) {
            break;
        }
        pos = nl + 1;
    }
    const double lh = size * 1.12;
    const double y0 = cy - (static_cast<double>(lines.size() - 1) * lh / 2.0);
    const double ax = cx + (align == "left" ? -hw : (align == "right" ? hw : 0.0));
    const std::string anchor = align == "left" ? "lm" : (align == "right" ? "rm" : "mm");
    // El warpAffine de OpenCV gira alrededor del centro del píxel (cx, cy); en Skia, +0,5.
    const double rotation = core::detail::py_num(m.contains("rotation") ? m.at("rotation") : Json(), 0.0);
    canvas.save();
    if (std::abs(rotation) > 0.01) {
        canvas.rotate(static_cast<SkScalar>(rotation), static_cast<SkScalar>(cx + 0.5), static_cast<SkScalar>(cy + 0.5));
    }
    SkFont sk_font(ff->typeface, static_cast<SkScalar>(size));
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto p = pillow_text(fonts, font, bold, size, lines[i], anchor);
        if (!p) {
            continue;
        }
        const double ox = ax + p->origin_x;
        const double oy = y0 + (static_cast<double>(i) * lh) + p->origin_y;
        const PillowLine line = pillow_line(*ff, lines[i], size);
        for (std::size_t g = 0; g < line.ids.size(); ++g) {
            if (auto path = sk_font.getPath(line.ids[g])) {
                canvas.drawPath(path->makeOffset(static_cast<SkScalar>(ox + line.x[g]), static_cast<SkScalar>(oy)),
                                paint);
            }
        }
    }
    canvas.restore();
    return true;
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
    SkMatrix xform = SkMatrix::I(); // giro y volteo del evento (\org, \frz, \frx180, \fry180)
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
    return path->makeTransform(run.xform * SkMatrix::Translate(run.pos[i].x(), run.pos[i].y()) *
                               SkMatrix::Scale(run.sx, run.sy));
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
    // El margen cubre 3σ, acotado: un \blur absurdo no puede desbordar el int ni pedir un mapa
    // enorme (con σ de cientos de px el halo ya se ha repartido por todo el cuadro).
    constexpr double kMaxBlurMargin = 1024.0;
    const double want = std::ceil(3 * sigma) + 1;
    const int margin = static_cast<int>(std::isfinite(want) ? std::clamp(want, 1.0, kMaxBlurMargin) : 1.0);
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
    SkMatrix xform = SkMatrix::I(); // gira con el texto
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
        canvas->drawPath(SkPath::Rect(box->rect).makeTransform(box->xform), p);
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

// Por qué aún no se dibuja un texto con este estilo, quieto o animado (nullopt = se dibuja).
std::optional<std::string> common_reason(const Json& st) {
    if (truthy(field(st, "italic")) || truthy(field(st, "underline"))) {
        return "cursiva o subrayado";
    }
    if (bold_of(st) && !real_bold(font_of(st))) {
        return "negrita sintética de libass (" + font_of(st) + ")";
    }
    return std::nullopt;
}

// Un texto quieto: además, solo las apariciones de bloque que van en sus tags (\fad, \move, \t).
std::optional<std::string> static_reason(const Json& st) {
    const Json& appear = field(st, "block_appear");
    if (truthy(appear) && !is_text(appear, "none") && !is_text(appear, "fade") && !is_text(appear, "typing") &&
        !is_text(appear, "pop") && !is_text(appear, "slide_up")) {
        return "aparición " + appear.dump();
    }
    return common_reason(st);
}

// `fontsize`: el \fs de un fotograma animado (_ass_font_size con 2 decimales) en vez del entero del
// estilo.
Result<TextParams> text_params(const Json& st, double height, const TextFonts& text_fonts,
                               std::optional<double> fontsize_override = std::nullopt) {
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
    const double fontsize = fontsize_override ? *fontsize_override : std::max(8.0, fs);
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
    double px = 0; // centro del bloque; en un texto quieto, entero (_xy)
    double py = 0;
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
    double rot = 0;         // giro en grados (horario, como el canvas), alrededor del centro del bloque
    bool flip_h = false;
    bool flip_v = false;
    double glyph_scale = 1; // Curva animada: el arco escala con la aparición (_anim_glyph_prefix)
};

// _rotation_tags: el giro (y el volteo) alrededor de \org como lo hace libass: primero \frz
// (antihorario en pantalla) y después el espejo de \frx180 / \fry180. Con un solo volteo, \frz
// lleva el signo contrario; los dos a la vez son un giro de 180°. Valores con _fmt.
SkMatrix rotation_matrix(double rot, double ox, double oy, bool fh, bool fv) {
    if (fh && fv) {
        rot += 180.0;
        fh = false;
        fv = false;
    }
    double frz = 0;
    if (!fh && !fv) {
        if (std::abs(rot) <= 0.05) {
            return SkMatrix::I();
        }
        frz = fmt2(-rot);
    } else if (std::abs(rot) > 0.05) {
        frz = fmt2(rot);
    }
    const auto x = static_cast<float>(fmt2(ox));
    const auto y = static_cast<float>(fmt2(oy));
    SkMatrix m = SkMatrix::Translate(x, y);
    m.preScale(fh ? -1.0F : 1.0F, fv ? -1.0F : 1.0F);
    m.preRotate(static_cast<float>(-frz));
    m.preTranslate(-x, -y);
    return m;
}

// El giro de los eventos de un texto: alrededor del centro del bloque, desplazado con la sombra.
SkMatrix event_matrix(const EventFrame& f, double off_x, double off_y) {
    return rotation_matrix(f.rot, f.px + off_x, f.py + off_y, f.flip_h, f.flip_v);
}

EventFrame event_frame(const TextParams& tp, const Json& st, const core::TextEvent& e, std::int64_t now,
                       double width, double height) {
    EventFrame f;
    f.tp = &tp;
    f.st = &st;
    f.width = width;
    f.height = height;
    const auto [ix, iy] = core::text_xy(st, width, height);
    f.px = static_cast<double>(ix);
    f.py = static_cast<double>(iy);
    f.rot = py_float(field(st, "rotation")).value_or(0.0);
    if (std::isnan(f.rot)) {
        f.rot = 0.0;
    }
    f.flip_h = truthy(field(st, "flip_h"));
    f.flip_v = truthy(field(st, "flip_v"));
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
    const double ax = f.px + (ax_raw - f.px) * f.sx + off_x;
    const double ry = f.py + off_y + row_dy * f.sy;
    double y = fmt2(ry);
    if (f.sliding) {
        // \move(ax, ry + dy, ax, ry, 0, 180): de fmt(ry + dy) a fmt(ry). Volteado en vertical,
        // libass refleja el recorrido alrededor del \org: el export invierte dy para que suba.
        const double dy = std::max(12.0, std::nearbyint(f.height * 0.035)) * (f.flip_v ? -1.0 : 1.0);
        const double y0 = fmt2(ry + dy);
        y = y0 + (fmt2(ry) - y0) * (dy == 0 ? 1.0 : 1.0 - f.slide / std::abs(dy));
    }
    return {static_cast<float>(fmt2(ax)), static_cast<float>(y)};
}

double anchor_x(const EventFrame& f) {
    const Json& align = field(*f.st, "align");
    return is_text(align, "left")    ? f.px - f.half + f.inset
           : is_text(align, "right") ? f.px + f.half - f.inset
                                     : f.px;
}

// Una línea colocada: los glifos de cada palabra con su escala y la caja de la línea (avance ×
// asc + desc de la palabra más alta).
struct PlacedLine {
    std::vector<Run> runs;
    SkRect box;
};

PlacedLine place_line(const EventFrame& f, const std::vector<std::string>& words, SkPoint pos,
                      const std::vector<std::pair<float, float>>& scales, bool center = false) {
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
    const double left = center                    ? pos.x() - pen / 2
                        : is_text(align, "left")  ? pos.x()
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
void draw_text_event(SkCanvas* canvas, const EventFrame& f, const core::TextContent& content,
                     const std::vector<std::vector<std::string>>& rows) {
    const TextParams& tp = *f.tp;
    const SkFont font = make_font(tp);
    const core::WordFx fx = core::word_fx_set(*f.st);
    const SkMatrix xform = event_matrix(f, 0, 0);
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
            active.push_back(content.is_active(gi + static_cast<std::int64_t>(w)));
            const double s = active.back() && fx.pop ? 1.18 : 1.0; // _fsc(st, 118) de la activa con pop
            scales.emplace_back(static_cast<float>(s * f.sx), static_cast<float>(s * f.sy));
        }
        PlacedLine pl = place_line(f, rows[i], line_pos(f, anchor_x(f), row_dy, 0, 0), scales);
        for (std::size_t w = 0; w < pl.runs.size(); ++w) {
            pl.runs[w].fill = active[w] ? tp.highlight : tp.fill;
            pl.runs[w].outline = tp.outline;
            pl.runs[w].alpha = mult_alpha(active[w] ? tp.alpha_active : tp.alpha_idle, f.fade);
            pl.runs[w].blur = active[w] && fx.glow ? 3.0 : 0.0;
            pl.runs[w].xform = xform;
        }
        std::optional<LineBox> box;
        if (tp.box && !tp.box_drawn) {
            box = LineBox{pl.box.makeOutset(static_cast<float>(tp.border), static_cast<float>(tp.border)), *tp.box,
                          mult_alpha(tp.box_alpha, f.fade), xform};
        }
        draw_line(canvas, font, pl.runs, tp.border, box, area);
        gi += static_cast<std::int64_t>(rows[i].size());
    }
}

// Evento de la sombra paralela o del halo del Brillo (capa 2i): la silueta (relleno y trazo en su
// color) desplazada, con su opacidad y \blur (shadow_dialogues y _shadow_override).
void draw_shadow_event(SkCanvas* canvas, const EventFrame& f, const core::TextShadow& sh,
                       const std::vector<std::vector<std::string>>& rows) {
    const TextParams& tp = *f.tp;
    const auto rgb = ass_color(core::ass_bgr(sh.color));
    if (!rgb) {
        return;
    }
    const SkFont font = make_font(tp);
    const int alpha = mult_alpha(hex_alpha(core::alpha_hex(sh.opacity * core::style_opacity(*f.st))), f.fade);
    const double blur = fmt2(sh.sigma / 0.85); // \blur{_fmt(σ / LIBASS_BLUR_SIGMA)}
    const double border = fmt2(tp.border_float + sh.spread);
    const SkMatrix xform = event_matrix(f, sh.dx, sh.dy);
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
            r.xform = xform;
        }
        draw_line(canvas, font, pl.runs, border, std::nullopt, area);
    }
}

// Curva (_curve_bodies con _glyph_prefix o _anim_glyph_prefix): una letra por evento, centrada
// (\an5) en su punto del arco y girada alrededor de sí misma (\org en la letra; \frz con el giro
// del texto más el de su sitio en el arco). Sin volteo: el export no lo pone en las letras. Con
// `sh`, los eventos de la sombra (la silueta desplazada en su color); si no, el texto con
// `content` (la palabra activa).
void draw_curve_event(SkCanvas* canvas, const EventFrame& f, const std::vector<core::CurveGlyph>& glyphs,
                      const core::TextContent* content, const core::TextShadow* sh) {
    const TextParams& tp = *f.tp;
    const SkFont font = make_font(tp);
    const core::WordFx wfx = core::word_fx_set(*f.st);
    const SkIRect area = SkIRect::MakeWH(static_cast<int>(f.width), static_cast<int>(f.height));
    const double off_x = sh != nullptr ? sh->dx : 0.0;
    const double off_y = sh != nullptr ? sh->dy : 0.0;
    std::optional<SkColor> sh_rgb;
    int sh_alpha = 0;
    double sh_blur = 0;
    double border = tp.border;
    if (sh != nullptr) {
        sh_rgb = ass_color(core::ass_bgr(sh->color));
        if (!sh_rgb) {
            return;
        }
        sh_alpha = mult_alpha(hex_alpha(core::alpha_hex(sh->opacity * core::style_opacity(*f.st))), f.fade);
        sh_blur = fmt2(sh->sigma / 0.85);
        border = fmt2(tp.border_float + sh->spread);
    }
    const double r = f.rot * std::numbers::pi / 180.0;
    for (const auto& g : glyphs) {
        if (core::detail::py_strip(g.ch).empty()) {
            continue; // `if ch.strip()`
        }
        const double gx = g.x * f.sx;
        const double gy = g.y * f.sy;
        const double x = f.px + off_x + (gx * std::cos(r) - gy * std::sin(r)) * f.glyph_scale;
        const double y = f.py + off_y + (gx * std::sin(r) + gy * std::cos(r)) * f.glyph_scale;
        double py = fmt2(y);
        if (f.sliding) { // \move(X, Y + dy, X, Y, 0, 180)
            const double dy = std::max(12.0, std::nearbyint(f.height * 0.035));
            const double y0 = fmt2(y + dy);
            py = y0 + (fmt2(y) - y0) * (1.0 - f.slide / dy);
        }
        const bool active = content != nullptr && content->is_active(g.word);
        const double sc = sh == nullptr && active && wfx.pop ? 1.18 : 1.0;
        PlacedLine pl = place_line(f, {g.ch}, {static_cast<float>(fmt2(x)), static_cast<float>(py)},
                                   {{static_cast<float>(sc * f.sx), static_cast<float>(sc * f.sy)}}, true);
        for (Run& run : pl.runs) {
            if (sh != nullptr) {
                run.fill = *sh_rgb;
                run.outline = *sh_rgb;
                run.alpha = sh_alpha;
                run.blur = sh_blur;
            } else {
                run.fill = active ? tp.highlight : tp.fill;
                run.outline = tp.outline;
                run.alpha = mult_alpha(active ? tp.alpha_active : tp.alpha_idle, f.fade);
                run.blur = active && wfx.glow ? 3.0 : 0.0;
            }
            run.xform = rotation_matrix(f.rot + g.angle * 180.0 / std::numbers::pi, x, y, false, false);
        }
        draw_line(canvas, font, pl.runs, border, std::nullopt, area);
    }
}

// Fondo dibujado (capa 2i, box_dialogues): un rectángulo redondeado por caja de box_rects, en su
// \pos y con la aparición del texto (la pop sí escala la forma: sus etiquetas no reponen \fscx).
// `rects` son las de box_rects; con `scale` (la aparición de un texto animado) crecen desde el
// centro del bloque.
void draw_box_event(SkCanvas* canvas, const EventFrame& f, const std::vector<core::BoxRect>& rects, double scale) {
    const TextParams& tp = *f.tp;
    const SkMatrix xform = event_matrix(f, 0, 0);
    const double radius = std::max(0.0, std::min(1.0, py_float(field(*f.st, "bg_radius")).value_or(0.0)));
    const double scale_x = f.popping ? f.pop_scale * f.sx : 1.0;
    const double scale_y = f.popping ? f.pop_scale * f.sy : 1.0;
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(with_alpha(*tp.box, mult_alpha(tp.box_alpha, f.fade)));
    for (const auto& b : rects) {
        // _rrect_path(w · escala · sx, h · escala · sy, radio) con las coordenadas a 2 decimales (_fmt)
        const double w = fmt2(b.w * scale * f.sx);
        const double h = fmt2(b.h * scale * f.sy);
        const SkPoint pos = line_pos(f, f.px + b.x * scale, b.y * scale, 0, 0);
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
            xform *
            SkMatrix::Translate(pos.x() - static_cast<float>(w * scale_x / 2), pos.y() - static_cast<float>(h * scale_y / 2)) *
            SkMatrix::Scale(static_cast<float>(scale_x), static_cast<float>(scale_y)));
        canvas->drawPath(path, p);
    }
}

// int(math.floor(t · 100 + 1e-6)): el instante en centésimas de animated_dialogues.
std::int64_t floor_cs(double t) { return static_cast<std::int64_t>(std::floor(t * 100 + 1e-6)); }

// _pos_frac: la posición en fracción del cuadro, acotada como en el preview y sin redondear.
double pos_frac(const Json& v) { return std::clamp(core::detail::py_num(v, 0.5), -10.0, 11.0); }

// Un fotograma de un texto animado (animated_dialogues): el fotograma k de la composición cae en su
// propio evento [⌊k/fps⌋, ⌊(k + 1)/fps⌋) en centésimas, con la pose de ESE fotograma y la aparición
// de bloque horneada (clip_fx_at), porque \fad, \move y \t se repetirían en cada evento. Dibuja,
// como allí, el Fondo (siempre como forma: _bg_draw), la sombra y el texto, si el evento se ve en
// `now`. El giro va alrededor del centro del bloque (_anim_center) y la aparición desplaza y escala
// desde él.
Result<void> draw_animated_text(SkCanvas* sk, const core::TimelineClip& c, Json base, std::int64_t k, std::int64_t now,
                                double fps, double width, double height, const TextFonts& fonts, bool buried,
                                bool omit, std::vector<std::string>* warnings) {
    const double dur = core::text_clip_duration(c);
    if (!(dur > 0)) {
        return {};
    }
    fps = std::max(1.0, fps);
    const double start = std::max(0.0, c.start);
    const double end = start + dur;
    const auto s_cs = static_cast<std::int64_t>(std::nearbyint(start * 100));
    const auto e_cs = static_cast<std::int64_t>(std::nearbyint(end * 100));
    const auto k_lo = static_cast<std::int64_t>(std::floor(start * fps + 1e-6));
    const auto k_hi = static_cast<std::int64_t>(std::ceil(end * fps - 1e-6));
    const auto window = [&](std::int64_t kk) {
        return std::pair{std::max(s_cs, floor_cs(static_cast<double>(kk) / fps)),
                         std::min(e_cs, floor_cs(static_cast<double>(kk + 1) / fps))};
    };
    if (k < k_lo || k >= k_hi) {
        return {};
    }
    if (const auto [a, b] = window(k); b <= a || now < a * 10 || now >= b * 10) {
        return {};
    }
    if (core::box_color(base)) {
        base["_bg_draw"] = true; // build_ass: el Fondo de un texto animado va como forma
    }
    core::TimelineClip appear;
    const Json& block = field(base, "block_appear");
    appear.appear = block.is_string() && !block.get_ref<const std::string&>().empty() ? block.get<std::string>()
                                                                                      : std::string("none");
    appear.exit = "none";
    const auto local_of = [&](std::int64_t kk) {
        return std::min(dur, std::max(0.0, static_cast<double>(kk) / fps - start));
    };
    const auto state_at = [&](std::int64_t kk, Json& st_out, core::ClipFx& fx_out) {
        const double local = local_of(kk);
        st_out = core::posed_text_style(c, base, local);
        fx_out = core::clip_fx_at(appear, local, dur);
        st_out["opacity"] = core::style_opacity(st_out) * fx_out.opacity;
        return core::text_content_at(c, st_out, local, dur);
    };
    Json st;
    core::ClipFx fx;
    const auto content = state_at(k, st, fx);
    if (!content) {
        return {};
    }
    // La máscara, la fusión y el giro 3D los pone quien llama (capa propia): aquí, el texto plano.
    std::optional<std::string> reason;
    if (buried) {
        reason = "pista de texto con vídeo encima";
    } else {
        reason = common_reason(st);
    }
    // El tamaño del fotograma: el de la letra con la escala de la aparición (\fs con 2 decimales).
    const double size = core::text_font_px(st, height);
    const double s = fx.scale;
    Json st_s = st;
    st_s["scale"] = core::text_style_scale(st) * s;
    Result<TextParams> tp = fail(ErrorCode::Unsupported, reason.value_or(""));
    if (!reason) {
        const double factor = core::ass_size_factor(font_of(st), bold_of(st), TextFontsAccess::impl(fonts).dirs);
        tp = text_params(st_s, height, fonts, fmt2(std::max(8.0, size * s) * factor));
    }
    if (!tp) {
        const std::string msg = std::format("texto {}: {} aún no se dibuja", c.id, tp.error().message);
        if (!omit) {
            return fail(ErrorCode::Unsupported, msg);
        }
        if (warnings != nullptr) {
            warnings->push_back(msg);
        }
        return {};
    }
    tp->border = fmt2(tp->border_float); // \bord del fotograma: float(border_width)
    // Las líneas se reparten una vez por texto visible, con el estilo del primer fotograma que lo
    // enseña (así no bailan con la escala). Con «escribir» el texto crece: el primero con este.
    std::int64_t k0 = k;
    if (is_text(field(base, "block_appear"), "typing")) {
        for (std::int64_t kk = k - 1; kk >= k_lo; --kk) {
            if (const auto [a, b] = window(kk); b <= a) {
                continue;
            }
            Json st_k;
            core::ClipFx fx_k;
            const auto seen = state_at(kk, st_k, fx_k);
            if (!seen || seen->text != content->text) {
                break;
            }
            k0 = kk;
        }
    } else {
        for (std::int64_t kk = k_lo; kk < k; ++kk) {
            if (const auto [a, b] = window(kk); b > a) {
                k0 = kk;
                break;
            }
        }
    }
    Json st0 = st;
    if (k0 != k) {
        core::ClipFx fx0;
        static_cast<void>(state_at(k0, st0, fx0));
    }
    const auto rows = core::text_rows(st0, width, height, content->text);

    // _anim_center y _anim_prefix: el centro (con el desplazamiento de la aparición, girado y
    // escalado), la caja escalada y el giro alrededor del centro.
    EventFrame f;
    f.tp = &*tp;
    f.st = &st_s;
    f.width = width;
    f.height = height;
    f.rot = core::detail::py_num(field(st, "rotation"), 0.0);
    const double rad = f.rot * std::numbers::pi / 180.0;
    const double ox = fx.tx * size * 2.2 * s;
    const double oy = fx.ty * size * 2.6 * s;
    f.px = pos_frac(field(st, "x")) * width + ox * std::cos(rad) - oy * std::sin(rad);
    f.py = pos_frac(field(st, "y")) * height + ox * std::sin(rad) + oy * std::cos(rad);
    const double wbox = core::detail::py_num(has(st, "w") ? field(st, "w") : Json(0.88), 0.88);
    f.half = py_max(0.2, py_min(1.0, wbox)) * width / 2.0 * s * core::text_style_scale(st);
    f.inset = 0.2 * size * s;
    f.lh = size * core::text_line_height(st) * s;
    std::tie(f.sx, f.sy) = core::stretch_of(st);
    f.flip_h = truthy(field(st, "flip_h"));
    f.flip_v = truthy(field(st, "flip_v"));
    // Capa 2i: el Fondo dibujado y la sombra; capa 2i + 1: el texto.
    if (core::box_drawn(st)) {
        draw_box_event(sk, f, core::box_rects(st, width, height, rows, tp->size_factor), s);
    }
    if (core::curve_of(st) != 0) { // la Curva no tiene Fondo (curve_of)
        f.glyph_scale = s;
        const auto glyphs = core::curve_glyphs(st, width, height, rows);
        if (const auto shadow = core::text_shadow(st, size)) {
            draw_curve_event(sk, f, glyphs, nullptr, &*shadow);
        }
        draw_curve_event(sk, f, glyphs, &*content, nullptr);
        return {};
    }
    if (const auto shadow = core::text_shadow(st, size)) {
        draw_shadow_event(sk, f, *shadow, rows);
    }
    draw_text_event(sk, f, *content, rows);
    return {};
}

const core::TimelineTrack* find_track(const core::Timeline& tl, const std::string& id) {
    for (const auto& t : tl.tracks) {
        if (t.id == id) {
            return &t;
        }
    }
    return nullptr;
}

// Lo común a todos los textos de un fotograma.
struct TextFrame {
    std::int64_t k = 0;
    std::int64_t now = 0;
    double fps = 30;
    double width = 0;
    double height = 0;
    const TextFonts* fonts = nullptr;
    bool omit = false;
    std::vector<std::string>* warnings = nullptr;

    [[nodiscard]] bool visible_at(const core::TextEvent& e) const { return e.start_ms <= now && now < e.end_ms; }

    // Un texto que aún no se sabe dibujar: error o, con `omit`, un aviso.
    [[nodiscard]] Result<void> unsupported(const std::string& msg) const {
        if (!omit) {
            return fail(ErrorCode::Unsupported, msg);
        }
        if (warnings != nullptr) {
            warnings->push_back(msg);
        }
        return {};
    }
};

// Los eventos de un texto (quieto o animado) en `sk`, como los dibujaría libass con su .ass.
Result<void> draw_clip_text(SkCanvas* sk, const core::TimelineClip& clip, const core::TimelineTrack& track,
                            const TextFrame& fr, bool buried) {
    const double width = fr.width;
    const double height = fr.height;
    const std::int64_t now = fr.now;
    const auto visible_at = [&fr](const core::TextEvent& e) { return fr.visible_at(e); };
    core::TimelineClip c = clip;
    const Json base = core::effective_text_style(track.style.value_or(Json()), clip.style.value_or(Json()));
    if (has(base, "text_case") && truthy(base.at("text_case"))) {
        c.text = core::apply_text_case(c.text.value_or(""), base.at("text_case"));
    }
    if (core::text_animates(c, fr.fps)) {
        return draw_animated_text(sk, c, base, fr.k, now, fr.fps, width, height, *fr.fonts, buried, fr.omit,
                                  fr.warnings);
    }
    const Json st = core::posed_text_style(c, base, 0.0);
    const double start = std::max(0.0, c.start);
    const double dur = core::text_clip_duration(c);
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
        return {};
    }
    std::optional<std::string> reason;
    if (buried) {
        reason = "pista de texto con vídeo encima";
    } else {
        reason = static_reason(st);
    }
    Result<TextParams> tp = reason ? Result<TextParams>(fail(ErrorCode::Unsupported, *reason))
                                   : text_params(st, height, *fr.fonts);
    if (!tp) {
        return fr.unsupported(std::format("texto {}: {} aún no se dibuja", c.id, tp.error().message));
    }
    // Capa 2i: el Fondo dibujado y después la sombra; capa 2i+1: el texto.
    const auto rows_of = [&](const std::string& text) { return core::text_rows(st, width, height, text); };
    for (const auto& e : under_events) {
        if (drawn_box && visible_at(e)) {
            draw_box_event(sk, event_frame(*tp, st, e, now, width, height),
                           core::box_rects(st, width, height, rows_of(e.content.text), tp->size_factor), 1.0);
        }
    }
    const bool curved = core::curve_of(st) != 0;
    for (const auto& e : under_events) {
        if (shadow && visible_at(e)) {
            const auto rows = rows_of(e.content.text);
            if (curved) {
                draw_curve_event(sk, event_frame(*tp, st, e, now, width, height),
                                 core::curve_glyphs(st, width, height, rows), nullptr, &*shadow);
            } else {
                draw_shadow_event(sk, event_frame(*tp, st, e, now, width, height), *shadow, rows);
            }
        }
    }
    for (const auto& e : text_events) {
        if (visible_at(e)) {
            const auto rows = rows_of(e.content.text);
            if (curved) {
                draw_curve_event(sk, event_frame(*tp, st, e, now, width, height),
                                 core::curve_glyphs(st, width, height, rows), &e.content, nullptr);
            } else {
                draw_text_event(sk, event_frame(*tp, st, e, now, width, height), e.content, rows);
            }
        }
    }
    return {};
}

// La homografía de `perspective` (sense=destination): las esquinas de la región (arriba-izq,
// arriba-der, abajo-izq, abajo-der) a `corners`, relativas a su origen, y de vuelta al cuadro.
// FFmpeg la aplica a índices de píxel (el píxel (x, y) de la salida toma la fuente en H⁻¹(x, y)),
// y Skia a coordenadas con el centro del píxel en +0,5: de ahí el medio píxel a cada lado.
SkMatrix warp_matrix(const core::WarpRegion& r, const core::Corners& corners) {
    const auto rw = static_cast<SkScalar>(r.x1 - r.x0);
    const auto rh = static_cast<SkScalar>(r.y1 - r.y0);
    const std::array<SkPoint, 4> src{{{0, 0}, {rw, 0}, {rw, rh}, {0, rh}}};
    const auto pt = [&](std::size_t i) {
        return SkPoint{static_cast<SkScalar>(corners[i].first), static_cast<SkScalar>(corners[i].second)};
    };
    const std::array<SkPoint, 4> dst{pt(0), pt(1), pt(3), pt(2)};
    SkMatrix h;
    if (!h.setPolyToPoly(src, dst)) {
        h.reset();
    }
    const auto x0 = static_cast<SkScalar>(r.x0);
    const auto y0 = static_cast<SkScalar>(r.y0);
    return SkMatrix::Translate(x0 + 0.5F, y0 + 0.5F) * h * SkMatrix::Translate(-x0 - 0.5F, -y0 - 0.5F);
}

// Un texto en capa propia (text_layer de compose.py): su .ass en una capa transparente del cuadro;
// si es 3D, la región de text_warp_spec deformada con `perspective`; con máscara, su alfa por el
// de la máscara; y fundida con lo de debajo con su modo (o encima).
Result<void> draw_own_layer(Canvas& canvas, const core::TimelineClip& c, const core::TimelineTrack& track,
                            const TextFrame& fr) {
    const Json base = core::effective_text_style(track.style.value_or(Json()), c.style.value_or(Json()));
    const auto warp = core::text_warp_spec(c, base, fr.width, fr.height, fr.fps);
    const std::string blend(core::clip_blend(c));
    std::optional<MaskAlpha> mask;
    if (core::has_mask(c)) {
        // La máscara del instante del clip (build_clip_mask: la de 0 si no se anima).
        const double local = std::min(core::text_clip_duration(c),
                                      std::max(0.0, (static_cast<double>(fr.k) / fr.fps) - std::max(0.0, c.start)));
        const auto masks = core::clip_masks_at(c, local);
        if (const std::string why = mask_unsupported(masks, fr.fonts); !why.empty()) {
            return fr.unsupported(std::format("texto {}: {}", c.id, why));
        }
        auto alpha = mask_alpha(masks, canvas.width(), canvas.height(), fr.fonts);
        if (!alpha && alpha.error().code == ErrorCode::Unsupported) {
            return fr.unsupported(std::format("texto {}: {}", c.id, alpha.error().message));
        }
        if (!alpha) {
            return std::unexpected(alpha.error());
        }
        mask = std::move(*alpha);
    }
    if (!mask && !warp && blend == "normal") {
        return {}; // como el export, que no la compone (no pasa con un texto de capa propia)
    }
    auto layer = Canvas::make(canvas.width(), canvas.height());
    if (!layer) {
        return std::unexpected(layer.error());
    }
    layer->clear({0, 0, 0, 0});
    if (auto ok = draw_clip_text(CanvasAccess::sk(*layer), c, track, fr, false); !ok) {
        return ok;
    }
    const sk_sp<SkImage> image = CanvasAccess::image(*layer);
    const bool own = mask.has_value() || blend != "normal";
    if (own) {
        canvas.begin_layer(blend);
    }
    SkCanvas* sk = CanvasAccess::sk(canvas);
    if (warp) {
        const core::WarpRegion& r = warp->region;
        sk->save();
        sk->clipRect(SkRect::MakeLTRB(static_cast<SkScalar>(r.x0), static_cast<SkScalar>(r.y0),
                                      static_cast<SkScalar>(r.x1), static_cast<SkScalar>(r.y1)));
        sk->concat(warp_matrix(r, warp->corners_at(fr.k)));
        sk->drawImage(image, 0, 0, SkSamplingOptions(SkFilterMode::kLinear), nullptr);
        sk->restore();
    } else {
        sk->drawImage(image, 0, 0);
    }
    if (mask) {
        return canvas.end_layer(*mask);
    }
    if (own) {
        canvas.end_layer();
    }
    return {};
}

} // namespace

Result<void> draw_timeline_texts(Canvas& canvas, const core::Timeline& timeline, std::int64_t k,
                                 const TextFonts& fonts, bool omit, std::vector<std::string>* warnings) {
    const core::ScopedTextWidth measure(fonts.width_fn()); // la de Pillow, solo mientras dibuja
    TextFrame fr;
    fr.k = k;
    fr.width = static_cast<double>(timeline.width - timeline.width % 2);
    fr.height = static_cast<double>(timeline.height - timeline.height % 2);
    fr.fps = timeline.fps > 0 ? static_cast<double>(timeline.fps) : 30.0;
    fr.now = core::ass_frame_ms(k, fr.fps);
    fr.fonts = &fonts;
    fr.omit = omit;
    fr.warnings = warnings;
    const auto buried = core::buried_text_track_ids(timeline.tracks);

    // Los textos que van al .ass (con su pista visible) y si van en capa propia.
    struct Entry {
        const core::TimelineClip* clip;
        const core::TimelineTrack* track;
        bool own;
    };
    std::vector<Entry> texts;
    for (const auto& cow : timeline.clips) {
        const core::TimelineClip& clip = *cow;
        if (clip.kind != "text" || !core::text_in_ass(clip)) {
            continue;
        }
        const core::TimelineTrack* track = find_track(timeline, clip.track_id);
        if (track == nullptr || track->hidden) {
            continue;
        }
        texts.push_back({&clip, track, core::text_own_layer(clip, fr.fps)});
    }
    // Primero los de capa propia sin vídeo encima, por orden de inicio (encima de todo el vídeo y
    // por debajo del .ass global); después, el .ass global en el orden de la lista.
    std::vector<const Entry*> own;
    for (const Entry& e : texts) {
        if (e.own && !buried.contains(e.clip->track_id)) {
            own.push_back(&e);
        }
    }
    std::ranges::stable_sort(own, {}, [](const Entry* e) { return e->clip->start; });
    for (const Entry* e : own) {
        if (auto ok = draw_own_layer(canvas, *e->clip, *e->track, fr); !ok) {
            return ok;
        }
    }
    SkCanvas* sk = CanvasAccess::sk(canvas);
    for (const Entry& e : texts) {
        const bool is_buried = buried.contains(e.clip->track_id);
        if (e.own && !is_buried) {
            continue;
        }
        if (auto ok = draw_clip_text(sk, *e.clip, *e.track, fr, is_buried); !ok) {
            return ok;
        }
    }
    return {};
}

} // namespace gc::render
