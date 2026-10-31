#include "gc/render/platform.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <numbers>
#include <optional>
#include <string_view>
#include <vector>

#include <hb.h>

#include "canvas_impl.hpp"
#include "include/core/SkData.h"
#include "include/core/SkFont.h"
#include "include/core/SkStream.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "include/core/SkTypeface.h"
#include "include/effects/SkGradient.h"
#include "include/effects/SkImageFilters.h"
#ifdef _WIN32
#include "include/ports/SkTypeface_win.h"
#endif

namespace gc::render {

namespace {

constexpr double kPi = std::numbers::pi;

// rgba() de CSS: el alfa a 0–255 redondeado, como el navegador.
constexpr SkColor rgba(unsigned r, unsigned g, unsigned b, double a) {
    return SkColorSetARGB(static_cast<U8CPU>(std::lround(std::clamp(a, 0.0, 1.0) * 255.0)), r, g, b);
}
constexpr SkColor kWhite = SK_ColorWHITE;

// Una fuente del sistema con su cara de HarfBuzz (la forma del texto, como Chromium).
struct SystemFace {
    sk_sp<SkTypeface> typeface;
    sk_sp<SkData> data;       // los bytes del archivo, que HarfBuzz lee sin copiarlos
    hb_face_t* hb = nullptr;  // vive lo que el proceso (como la caché de fuentes del navegador)
};

// system-ui: la fuente del sistema (Segoe UI en Windows, la de Chromium), con el peso por la regla
// de CSS (500 cae en 400, 800 en 900…). Una por peso, cargada una vez.
const SystemFace* system_ui(int weight) {
    static std::mutex mutex;
    static sk_sp<SkFontMgr> mgr;
    static std::array<SystemFace, 10> faces;
    const std::scoped_lock lock(mutex);
    const auto slot = static_cast<std::size_t>(std::clamp(weight / 100, 1, 9));
    SystemFace& sf = faces[slot];
    if (sf.hb != nullptr) {
        return &sf;
    }
#ifdef _WIN32
    if (!mgr) {
        mgr = SkFontMgr_New_DirectWrite();
    }
#endif
    if (!mgr) {
        return nullptr;
    }
    for (const char* family : {"Segoe UI", "Arial"}) {
        if (auto set = mgr->matchFamily(family); set && set->count() > 0) {
            // matchStyle de DirectWrite es el matchStyleCSS3 de Skia.
            sf.typeface = set->matchStyle(SkFontStyle(static_cast<int>(slot) * 100, SkFontStyle::kNormal_Width,
                                                      SkFontStyle::kUpright_Slant));
            if (sf.typeface) {
                break;
            }
        }
    }
    if (!sf.typeface) {
        return nullptr;
    }
    int index = 0;
    auto stream = sf.typeface->openStream(&index);
    sf.data = stream ? SkData::MakeFromStream(stream.get(), stream->getLength()) : nullptr;
    if (!sf.data) {
        return nullptr;
    }
    hb_blob_t* blob = hb_blob_create(static_cast<const char*>(sf.data->data()), static_cast<unsigned>(sf.data->size()),
                                     HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    sf.hb = hb_face_create(blob, static_cast<unsigned>(index));
    hb_blob_destroy(blob);
    return &sf;
}

// Los glifos de una línea con la forma de HarfBuzz (interletraje incluido) a `size` px, en
// posiciones de 1/65536 de px (las de Chromium), y su avance total.
struct Shaped {
    std::vector<SkGlyphID> ids;
    std::vector<SkPoint> pos;
    double width = 0;
};

Shaped shape(const SystemFace& sf, std::string_view text, double size) {
    Shaped out;
    hb_font_t* font = hb_font_create(sf.hb);
    const int scale = static_cast<int>(std::lround(size * 65536.0));
    hb_font_set_scale(font, scale, scale);
    hb_buffer_t* buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, text.data(), static_cast<int>(text.size()), 0, static_cast<int>(text.size()));
    hb_buffer_guess_segment_properties(buf);
    hb_shape(font, buf, nullptr, 0);
    unsigned n = 0;
    const hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &n);
    const hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &n);
    double pen = 0;
    for (unsigned i = 0; i < n; ++i) {
        out.ids.push_back(static_cast<SkGlyphID>(info[i].codepoint));
        out.pos.push_back(SkPoint::Make(static_cast<float>(pen + (pos[i].x_offset / 65536.0)),
                                        static_cast<float>(-pos[i].y_offset / 65536.0)));
        pen += pos[i].x_advance / 65536.0;
    }
    out.width = pen;
    hb_buffer_destroy(buf);
    hb_font_destroy(font);
    return out;
}

// Lo que hace falta del contexto 2D del navegador, sobre un SkCanvas: el estado de save/restore,
// el trazado actual (que no es parte del estado) y la sombra de cada dibujo.
class Ctx2D {
public:
    enum class Align : std::uint8_t { Left, Center };
    enum class Baseline : std::uint8_t { Alphabetic, Top, Middle };

    struct State {
        SkColor fill = SK_ColorBLACK;
        sk_sp<SkShader> fill_shader;
        SkColor stroke = SK_ColorBLACK;
        float line_width = 1.0F;
        SkPaint::Cap cap = SkPaint::kButt_Cap;
        SkPaint::Join join = SkPaint::kMiter_Join;
        SkColor shadow_color = SK_ColorTRANSPARENT;
        float shadow_blur = 0.0F;
        SkBlendMode blend = SkBlendMode::kSrcOver;
        int font_weight = 400;
        float font_size = 10.0F;
        Align align = Align::Left; // «start» con texto de izquierda a derecha
        Baseline baseline = Baseline::Alphabetic;
    };

    explicit Ctx2D(SkCanvas* canvas) : canvas_(canvas) {}

    State& s() { return state_; }

    void save() {
        stack_.push_back(state_);
        canvas_->save();
    }
    void restore() {
        if (stack_.empty()) {
            return;
        }
        state_ = stack_.back();
        stack_.pop_back();
        canvas_->restore();
    }
    void translate(double x, double y) { canvas_->translate(f(x), f(y)); }
    void rotate(double radians) { canvas_->rotate(f(radians * 180.0 / kPi)); }

    void begin_path() {
        path_ = SkPathBuilder();
        has_point_ = false;
    }
    void move_to(double x, double y) {
        path_.moveTo(f(x), f(y));
        has_point_ = true;
    }
    void line_to(double x, double y) {
        if (!has_point_) {
            move_to(x, y);
            return;
        }
        path_.lineTo(f(x), f(y));
    }
    void bezier_to(double c1x, double c1y, double c2x, double c2y, double x, double y) {
        ensure_point(c1x, c1y);
        path_.cubicTo(f(c1x), f(c1y), f(c2x), f(c2y), f(x), f(y));
    }
    void quad_to(double cx, double cy, double x, double y) {
        ensure_point(cx, cy);
        path_.quadTo(f(cx), f(cy), f(x), f(y));
    }
    void close_path() { path_.close(); }

    // arc(): como el navegador, una línea desde el punto actual hasta el principio del arco; en el
    // sentido de las agujas (y hacia abajo), el barrido es (fin − inicio) módulo 2π, y una vuelta
    // entera si llega a 2π (Chromium la parte en dos medias vueltas).
    void arc(double cx, double cy, double r, double a0, double a1) {
        double sweep = a1 - a0;
        const bool full = sweep >= 2 * kPi;
        if (!full) {
            sweep = std::fmod(sweep, 2 * kPi);
            if (sweep < 0) {
                sweep += 2 * kPi;
            }
        }
        const SkRect oval = SkRect::MakeLTRB(f(cx - r), f(cy - r), f(cx + r), f(cy + r));
        const double start = a0 * 180.0 / kPi;
        if (full) {
            path_.arcTo(oval, f(start), 180.0F, !has_point_);
            path_.arcTo(oval, f(start + 180.0), 180.0F, false);
        } else {
            path_.arcTo(oval, f(start), f(sweep * 180.0 / kPi), !has_point_);
        }
        has_point_ = true;
    }

    // roundRect con un radio (ya acotado por quien llama, como rrect de platformChrome.js).
    void round_rect(double x, double y, double w, double h, double r) {
        path_.addRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(f(x), f(y), f(w), f(h)), f(r), f(r)));
        has_point_ = true;
    }
    void rect(double x, double y, double w, double h) {
        path_.addRect(SkRect::MakeXYWH(f(x), f(y), f(w), f(h)));
        has_point_ = true;
    }
    void clip() { canvas_->clipPath(path_.snapshot(), true); }

    void fill() { canvas_->drawPath(path_.snapshot(), paint(false)); }
    void stroke() { canvas_->drawPath(path_.snapshot(), paint(true)); }
    void fill_rect(double x, double y, double w, double h) {
        canvas_->drawRect(SkRect::MakeXYWH(f(x), f(y), f(w), f(h)), paint(false));
    }

    void fill_text(std::string_view text, double x, double y) {
        const SystemFace* sf = system_ui(state_.font_weight);
        if (sf == nullptr) {
            return;
        }
        SkFont font(sf->typeface, state_.font_size);
        font.setSubpixel(true);
        font.setEdging(SkFont::Edging::kAntiAlias);
        const Shaped run = shape(*sf, text, state_.font_size);
        double dx = x;
        if (state_.align == Align::Center) {
            dx -= run.width / 2.0;
        }
        // «top» y «middle» van por la caja del em, como Chromium (NormalizedTypoAscentAndDescent).
        double dy = y;
        if (state_.baseline != Baseline::Alphabetic) {
            const double ascent = em_ascent(*sf->typeface, font);
            const double descent = state_.font_size - ascent;
            dy += state_.baseline == Baseline::Top ? ascent : (ascent - descent) / 2.0;
        }
        canvas_->drawGlyphs(run.ids, run.pos, SkPoint::Make(f(dx), f(dy)), font, paint(false));
    }

private:
    static float f(double v) { return static_cast<float>(v); }

    // El ascenso de la caja del em (SimpleFontData::NormalizedTypoAscentAndDescent de Chromium):
    // sTypoAscender y sTypoDescender de la tabla OS/2 llevados a que sumen el tamaño, con su
    // proporción, a 1/64 de px (LayoutUnit); si no valen, el ascenso y la altura de la fuente.
    [[nodiscard]] double em_ascent(const SkTypeface& face, const SkFont& font) const {
        const double size = state_.font_size;
        const auto normalized = [&](double ascent, double height) -> std::optional<double> {
            if (!(height > 0) || ascent < 0 || ascent > height) {
                return std::nullopt;
            }
            return std::round(ascent * size / height * 64.0) / 64.0;
        };
        std::array<std::uint8_t, 4> raw{};
        if (face.getTableData(SkSetFourByteTag('O', 'S', '/', '2'), 68, raw.size(), raw.data()) == raw.size()) {
            const auto be16 = [&](std::size_t i) {
                return static_cast<std::int16_t>(static_cast<std::uint16_t>((raw[i] << 8U) | raw[i + 1]));
            };
            const double typo_ascender = be16(0);
            const double typo_descender = -static_cast<double>(be16(2));
            if (const auto a = normalized(typo_ascender, typo_ascender + typo_descender)) {
                return *a;
            }
        }
        SkFontMetrics m{};
        font.getMetrics(&m);
        return normalized(-m.fAscent, m.fDescent - m.fAscent).value_or(-m.fAscent);
    }

    void ensure_point(double x, double y) {
        if (!has_point_) {
            move_to(x, y);
        }
    }

    [[nodiscard]] SkPaint paint(bool stroke) const {
        SkPaint p;
        p.setAntiAlias(true);
        if (stroke) {
            p.setStyle(SkPaint::kStroke_Style);
            p.setColor(state_.stroke);
            p.setStrokeWidth(state_.line_width);
            p.setStrokeCap(state_.cap);
            p.setStrokeJoin(state_.join);
        } else {
            p.setColor(state_.fill_shader ? SK_ColorBLACK : state_.fill);
            p.setShader(state_.fill_shader);
        }
        p.setBlendMode(state_.blend);
        // La sombra del contexto 2D (sin desplazamiento): un difuminado de sigma = shadowBlur / 2.
        if (SkColorGetA(state_.shadow_color) != 0 && state_.shadow_blur > 0) {
            const float sigma = state_.shadow_blur / 2.0F;
            p.setImageFilter(SkImageFilters::DropShadow(0, 0, sigma, sigma, state_.shadow_color, nullptr));
        }
        return p;
    }

    SkCanvas* canvas_;
    State state_;
    std::vector<State> stack_;
    SkPathBuilder path_;
    bool has_point_ = false;
};

using Align = Ctx2D::Align;
using Baseline = Ctx2D::Baseline;

void set_font(Ctx2D& ctx, int weight, double size) {
    ctx.s().font_weight = weight;
    ctx.s().font_size = static_cast<float>(size);
}

void rrect(Ctx2D& ctx, double x, double y, double w, double h, double r) {
    const double rad = std::min({r, w / 2, h / 2});
    ctx.begin_path();
    ctx.round_rect(x, y, w, h, rad);
}

// --- Glifos (centrados en cx, cy; tamaño s ≈ la mitad del icono) ---
void heart(Ctx2D& ctx, double cx, double cy, double s) {
    const double t = s * 0.95;
    ctx.begin_path();
    ctx.move_to(cx, cy + (t * 0.7));
    ctx.bezier_to(cx - (t * 1.35), cy - (t * 0.25), cx - (t * 0.6), cy - (t * 1.15), cx, cy - (t * 0.32));
    ctx.bezier_to(cx + (t * 0.6), cy - (t * 1.15), cx + (t * 1.35), cy - (t * 0.25), cx, cy + (t * 0.7));
    ctx.close_path();
    ctx.fill();
}

void bubble(Ctx2D& ctx, double cx, double cy, double s) {
    rrect(ctx, cx - s, cy - (s * 0.85), s * 2, s * 1.5, s * 0.5);
    ctx.fill();
    ctx.begin_path();
    ctx.move_to(cx - (s * 0.55), cy + (s * 0.5));
    ctx.line_to(cx - (s * 0.95), cy + (s * 1.05));
    ctx.line_to(cx - (s * 0.1), cy + (s * 0.6));
    ctx.close_path();
    ctx.fill();
}

void bookmark(Ctx2D& ctx, double cx, double cy, double s) {
    ctx.begin_path();
    ctx.move_to(cx - (s * 0.72), cy - s);
    ctx.line_to(cx + (s * 0.72), cy - s);
    ctx.line_to(cx + (s * 0.72), cy + s);
    ctx.line_to(cx, cy + (s * 0.45));
    ctx.line_to(cx - (s * 0.72), cy + s);
    ctx.close_path();
    ctx.fill();
}

void share_arrow(Ctx2D& ctx, double cx, double cy, double s) {
    const float lw = ctx.s().line_width;
    ctx.begin_path();
    ctx.move_to(cx - s, cy + (s * 0.5));
    ctx.quad_to(cx - s, cy - (s * 0.5), cx + (s * 0.3), cy - (s * 0.55));
    ctx.stroke();
    ctx.begin_path();
    ctx.move_to(cx - (s * 0.1), cy - s);
    ctx.line_to(cx + s, cy - (s * 0.55));
    ctx.line_to(cx - (s * 0.1), cy - (s * 0.1));
    ctx.stroke();
    ctx.s().line_width = lw;
}

void thumb(Ctx2D& ctx, double cx, double cy, double s, bool down) {
    ctx.save();
    ctx.translate(cx, cy);
    if (down) {
        ctx.rotate(kPi);
    }
    rrect(ctx, -s * 0.95, s * 0.05, s * 0.5, s * 0.9, s * 0.08);
    ctx.fill();
    rrect(ctx, -s * 0.38, -s * 0.2, s * 1.15, s * 1.15, s * 0.16);
    ctx.fill();
    rrect(ctx, -s * 0.12, -s * 0.95, s * 0.5, s * 0.85, s * 0.22);
    ctx.fill();
    ctx.restore();
}

void music_note(Ctx2D& ctx, double cx, double cy, double s) {
    const float lw = ctx.s().line_width;
    ctx.s().line_width = static_cast<float>(std::max(1.0, s * 0.28));
    ctx.begin_path();
    ctx.move_to(cx - (s * 0.35), cy + (s * 0.55));
    ctx.line_to(cx - (s * 0.35), cy - (s * 0.7));
    ctx.line_to(cx + (s * 0.55), cy - s);
    ctx.line_to(cx + (s * 0.55), cy + (s * 0.25));
    ctx.stroke();
    ctx.begin_path();
    ctx.arc(cx - (s * 0.6), cy + (s * 0.55), s * 0.32, 0, kPi * 2);
    ctx.arc(cx + (s * 0.3), cy + (s * 0.25), s * 0.32, 0, kPi * 2);
    ctx.fill();
    ctx.s().line_width = lw;
}

void disc(Ctx2D& ctx, double cx, double cy, double r) {
    ctx.begin_path();
    ctx.arc(cx, cy, r, 0, kPi * 2);
    ctx.fill();
    ctx.save();
    ctx.s().blend = SkBlendMode::kDstOut;
    ctx.begin_path();
    ctx.arc(cx, cy, r * 0.32, 0, kPi * 2);
    ctx.fill();
    ctx.restore();
}

void set_fill(Ctx2D& ctx, SkColor color) {
    ctx.s().fill = color;
    ctx.s().fill_shader = nullptr;
}

void avatar(Ctx2D& ctx, double cx, double cy, double r) {
    ctx.save();
    ctx.begin_path();
    ctx.arc(cx, cy, r, 0, kPi * 2);
    set_fill(ctx, rgba(255, 255, 255, 0.28));
    ctx.fill();
    ctx.s().line_width = static_cast<float>(std::max(1.5, r * 0.14));
    ctx.s().stroke = kWhite;
    ctx.stroke();
    // silueta
    set_fill(ctx, kWhite);
    ctx.begin_path();
    ctx.arc(cx, cy - (r * 0.28), r * 0.32, 0, kPi * 2);
    ctx.fill();
    ctx.begin_path();
    ctx.arc(cx, cy + (r * 0.55), r * 0.55, kPi, 0);
    ctx.fill();
    ctx.restore();
}

void label(Ctx2D& ctx, std::string_view text, double cx, double cy, double size, int weight = 600) {
    ctx.save();
    set_font(ctx, weight, size);
    ctx.s().align = Align::Center;
    ctx.s().baseline = Baseline::Top;
    set_fill(ctx, kWhite);
    ctx.fill_text(text, cx, cy);
    ctx.restore();
}

struct Frame {
    double x, y, w, h;
    [[nodiscard]] double X(double fr) const { return x + (fr * w); }
    [[nodiscard]] double Y(double fr) const { return y + (fr * h); }
};

void scrim(Ctx2D& ctx, const Frame& fr) {
    const std::array<SkPoint, 2> pts{SkPoint::Make(0, static_cast<float>(fr.Y(0.62))),
                                     SkPoint::Make(0, static_cast<float>(fr.Y(1)))};
    const std::array<SkColor4f, 2> colors{SkColor4f::FromColor(rgba(0, 0, 0, 0)), SkColor4f::FromColor(rgba(0, 0, 0, 0.42))};
    const SkGradient grad(SkGradient::Colors(colors, SkTileMode::kClamp), SkGradient::Interpolation{});
    const SkColor before = ctx.s().fill;
    ctx.s().fill_shader = SkShaders::LinearGradient(pts.data(), grad);
    ctx.fill_rect(fr.X(0), fr.Y(0.62), fr.w, fr.h * 0.38);
    // fillStyle sigue siendo el degradado hasta el siguiente fillStyle (lo pone cada glifo).
    ctx.s().fill = before;
}

void common_setup(Ctx2D& ctx, const Frame& fr) {
    ctx.s().line_width = static_cast<float>(std::max(1.5, fr.w * 0.008));
    ctx.s().cap = SkPaint::kRound_Cap;
    ctx.s().join = SkPaint::kRound_Join;
    set_fill(ctx, kWhite);
    ctx.s().stroke = kWhite;
    ctx.s().shadow_color = rgba(0, 0, 0, 0.5);
    ctx.s().shadow_blur = static_cast<float>(fr.w * 0.02);
    scrim(ctx, fr);
}

void draw_tiktok(Ctx2D& ctx, const Frame& fr) {
    const double W = fr.w;
    const double R = W * 0.052;
    common_setup(ctx, fr);

    const double cnt = W * 0.031;
    // Barra superior: Siguiendo · Para ti
    label(ctx, "Siguiendo", fr.X(0.36), fr.Y(0.045), W * 0.036, 500);
    label(ctx, "Para ti", fr.X(0.62), fr.Y(0.045), W * 0.04, 800);
    ctx.begin_path();
    ctx.move_to(fr.X(0.585), fr.Y(0.088));
    ctx.line_to(fr.X(0.655), fr.Y(0.088));
    ctx.stroke();

    // Rail derecho
    const double rx = fr.X(0.9);
    avatar(ctx, rx, fr.Y(0.5), R);
    // badge +
    ctx.save();
    ctx.s().shadow_blur = 0;
    set_fill(ctx, SkColorSetRGB(0xFE, 0x2C, 0x55));
    ctx.begin_path();
    ctx.arc(rx, fr.Y(0.5) + R, R * 0.42, 0, kPi * 2);
    ctx.fill();
    ctx.s().stroke = kWhite;
    ctx.s().line_width = static_cast<float>(std::max(1.5, R * 0.12));
    const double p = R * 0.22;
    ctx.begin_path();
    ctx.move_to(rx - p, fr.Y(0.5) + R);
    ctx.line_to(rx + p, fr.Y(0.5) + R);
    ctx.move_to(rx, fr.Y(0.5) + R - p);
    ctx.line_to(rx, fr.Y(0.5) + R + p);
    ctx.stroke();
    ctx.restore();

    set_fill(ctx, kWhite);
    heart(ctx, rx, fr.Y(0.62), R * 0.9);
    label(ctx, "328,5 K", rx, fr.Y(0.62) + R, cnt);
    bubble(ctx, rx, fr.Y(0.73), R * 0.85);
    label(ctx, "1204", rx, fr.Y(0.73) + R, cnt);
    bookmark(ctx, rx, fr.Y(0.82), R * 0.82);
    label(ctx, "5342", rx, fr.Y(0.82) + R, cnt);
    share_arrow(ctx, rx, fr.Y(0.905), R * 0.85);
    label(ctx, "Compartir", rx, fr.Y(0.905) + R, cnt);

    // Disco de música girando (abajo derecha)
    ctx.save();
    set_fill(ctx, rgba(30, 30, 30, 0.9));
    disc(ctx, rx, fr.Y(0.965), R * 0.9);
    ctx.restore();
    music_note(ctx, rx, fr.Y(0.965), R * 0.5);

    // Texto inferior izquierdo
    ctx.s().align = Align::Left;
    ctx.s().baseline = Baseline::Alphabetic;
    set_font(ctx, 700, W * 0.042);
    ctx.fill_text("@usuario", fr.X(0.04), fr.Y(0.9));
    set_font(ctx, 500, W * 0.036);
    ctx.fill_text("Descripción del vídeo #fyp #viral", fr.X(0.04), fr.Y(0.945));
    music_note(ctx, fr.X(0.06), fr.Y(0.978), W * 0.02);
    set_font(ctx, 500, W * 0.032);
    ctx.fill_text("sonido original - usuario", fr.X(0.1), fr.Y(0.983));
}

void draw_shorts(Ctx2D& ctx, const Frame& fr) {
    const double W = fr.w;
    const double R = W * 0.05;
    common_setup(ctx, fr);

    const double cnt = W * 0.03;
    // Superior: ← ... Shorts ... 🔍 ⋮
    ctx.begin_path();
    ctx.move_to(fr.X(0.1), fr.Y(0.05));
    ctx.line_to(fr.X(0.05), fr.Y(0.05));
    ctx.move_to(fr.X(0.07), fr.Y(0.035));
    ctx.line_to(fr.X(0.05), fr.Y(0.05));
    ctx.line_to(fr.X(0.07), fr.Y(0.065));
    ctx.stroke();
    ctx.save();
    ctx.s().shadow_blur = 0;
    set_fill(ctx, SkColorSetRGB(0xFF, 0x00, 0x33));
    rrect(ctx, fr.X(0.12), fr.Y(0.03), W * 0.05, W * 0.05, W * 0.012);
    ctx.fill();
    set_fill(ctx, kWhite);
    set_font(ctx, 800, W * 0.036);
    ctx.s().align = Align::Left;
    ctx.s().baseline = Baseline::Middle;
    ctx.fill_text("Shorts", fr.X(0.185), fr.Y(0.055));
    ctx.restore();
    // buscar + kebab
    ctx.save();
    ctx.s().shadow_blur = static_cast<float>(W * 0.02);
    ctx.s().stroke = kWhite;
    ctx.begin_path();
    ctx.arc(fr.X(0.86), fr.Y(0.05), W * 0.022, 0, kPi * 2);
    ctx.stroke();
    ctx.begin_path();
    ctx.move_to(fr.X(0.878), fr.Y(0.068));
    ctx.line_to(fr.X(0.9), fr.Y(0.088));
    ctx.stroke();
    set_fill(ctx, kWhite);
    for (int i = 0; i < 3; ++i) {
        ctx.begin_path();
        ctx.arc(fr.X(0.95), fr.Y(0.038) + (i * W * 0.024), W * 0.007, 0, kPi * 2);
        ctx.fill();
    }
    ctx.restore();

    // Rail derecho
    const double rx = fr.X(0.91);
    set_fill(ctx, kWhite);
    thumb(ctx, rx, fr.Y(0.5), R * 0.95, false);
    label(ctx, "45 K", rx, fr.Y(0.5) + R, cnt);
    thumb(ctx, rx, fr.Y(0.61), R * 0.95, true);
    label(ctx, "No me gusta", rx, fr.Y(0.61) + R, cnt);
    bubble(ctx, rx, fr.Y(0.72), R * 0.85);
    label(ctx, "890", rx, fr.Y(0.72) + R, cnt);
    share_arrow(ctx, rx, fr.Y(0.82), R * 0.85);
    label(ctx, "Compartir", rx, fr.Y(0.82) + R, cnt);
    // remezclar (dos flechas)
    ctx.save();
    ctx.begin_path();
    ctx.arc(rx, fr.Y(0.9), R * 0.7, 0.2, kPi - 0.2);
    ctx.stroke();
    ctx.begin_path();
    ctx.arc(rx, fr.Y(0.9), R * 0.7, kPi + 0.2, -0.2);
    ctx.stroke();
    ctx.restore();
    label(ctx, "Remezclar", rx, fr.Y(0.9) + (R * 0.9), cnt);
    // álbum/sonido (cuadrado girado)
    ctx.save();
    ctx.s().shadow_blur = 0;
    set_fill(ctx, rgba(30, 30, 30, 0.92));
    rrect(ctx, rx - (R * 0.55), fr.Y(0.965) - (R * 0.55), R * 1.1, R * 1.1, R * 0.2);
    ctx.fill();
    ctx.restore();
    music_note(ctx, rx, fr.Y(0.965), R * 0.45);

    // Inferior izquierdo: avatar + @canal + Suscribirse
    ctx.save();
    avatar(ctx, fr.X(0.07), fr.Y(0.9), W * 0.032);
    ctx.s().shadow_blur = static_cast<float>(W * 0.02);
    ctx.s().align = Align::Left;
    ctx.s().baseline = Baseline::Middle;
    set_fill(ctx, kWhite);
    set_font(ctx, 700, W * 0.036);
    ctx.fill_text("@canal", fr.X(0.12), fr.Y(0.9));
    // botón Suscribirse (pastilla blanca)
    ctx.s().shadow_blur = 0;
    const double bw = W * 0.24;
    const double bh = W * 0.055;
    const double byy = fr.Y(0.9) - (bh / 2);
    const double bxx = fr.X(0.28);
    set_fill(ctx, kWhite);
    rrect(ctx, bxx, byy, bw, bh, bh / 2);
    ctx.fill();
    set_fill(ctx, SK_ColorBLACK);
    ctx.s().align = Align::Center;
    set_font(ctx, 700, W * 0.03);
    ctx.fill_text("Suscribirse", bxx + (bw / 2), fr.Y(0.9));
    ctx.restore();

    ctx.save();
    ctx.s().shadow_blur = static_cast<float>(W * 0.02);
    set_fill(ctx, kWhite);
    ctx.s().align = Align::Left;
    ctx.s().baseline = Baseline::Alphabetic;
    set_font(ctx, 500, W * 0.034);
    ctx.fill_text("Título del short #shorts", fr.X(0.04), fr.Y(0.96));
    music_note(ctx, fr.X(0.06), fr.Y(0.988), W * 0.018);
    set_font(ctx, 500, W * 0.03);
    ctx.fill_text("Sonido original", fr.X(0.1), fr.Y(0.992));
    ctx.restore();
}

} // namespace

void draw_platform_chrome(Canvas& canvas, double x, double y, double w, double h, std::string_view kind) {
    if (kind.empty() || kind == "none" || !(w > 0)) {
        return;
    }
    Ctx2D ctx(CanvasAccess::sk(canvas));
    ctx.save();
    ctx.begin_path();
    ctx.rect(x, y, w, h);
    ctx.clip();
    const Frame fr{x, y, w, h};
    if (kind == "tiktok") {
        draw_tiktok(ctx, fr);
    } else if (kind == "shorts") {
        draw_shorts(ctx, fr);
    }
    ctx.restore();
}

} // namespace gc::render
