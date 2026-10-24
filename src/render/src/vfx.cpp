#include "gc/render/vfx.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <charconv>
#include <format>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/export_input.hpp"
#include "gc/core/fx_bake.hpp"
#include "gc/core/fx_catalog.hpp"
#include "gc/core/timeline_rules.hpp"
#include "gc/core/ids.hpp"
#include "gc/core/vfx_stack.hpp"
#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkSurface.h"
#include "include/effects/SkRuntimeEffect.h"
#include "paper.hpp"

namespace gc::render {

namespace {

#include "vfx_shaders.inc"

constexpr double kFxFps = 30.0; // cadencia de `frame` (el azar por fotograma), fija para todos

std::mutex& assets_mutex() {
    static std::mutex m;
    return m;
}
std::filesystem::path& assets_dir() {
    static std::filesystem::path dir;
    return dir;
}

// Los efectos que el render sabe dibujar: los de motor «shader» y paper_cutout.
bool drawable(const Json& manifest) {
    const Json engine = manifest.value("engine", Json());
    return engine == Json("shader") || (engine == Json("canvas") && manifest.value("id", Json()) == Json("paper_cutout"));
}

// --- El GLSL de los efectos y su traducción a SkSL ----------------------------------------------

// NOLINTNEXTLINE(bugprone-exception-escape): el move de std::map de MSVC reserva su nodo (bad_alloc)
struct ShaderTable {
    std::string prelude_sksl;
    std::optional<std::string> error; // el preludio cambió y ya no se sabe traducir
    std::map<std::string, Json, std::less<>> effects; // id → {id, v, pad, params, frag}
};

std::string joined(std::string_view chunks) { return std::string(chunks); }

// La textura de WebGL (LINEAR + CLAMP_TO_EDGE) como la filtra la GPU: bilineal entre los cuatro
// texels vecinos con los pesos en 8 bits de fracción (1/256), como NVIDIA, AMD e Intel. Skia
// interpolaría con pesos exactos, y en los efectos con umbral (pixel_drift) eso cambia qué
// muestra gana. El hijo `uSrc` se muestrea al vecino más cercano, en el centro de cada texel.
// Y lo de GLSL ES 3 que el SkSL de los efectos (ES 2) no tiene: transpose.
constexpr std::string_view kBilinear = R"(vec4 gc_texel(vec2 i) {
  return vec4(uSrc.eval(clamp(i, vec2(0.0), uSrcSize - 1.0) + 0.5));
}
vec4 gc_bilinear(vec2 t) {
  vec2 p = t * uSrcSize - 0.5;
  vec2 i = floor(p);
  vec2 f = floor((p - i) * 256.0 + 0.5) / 256.0;
  vec4 top = mix(gc_texel(i), gc_texel(i + vec2(1.0, 0.0)), f.x);
  vec4 bottom = mix(gc_texel(i + vec2(0.0, 1.0)), gc_texel(i + vec2(1.0, 1.0)), f.x);
  return mix(top, bottom, f.y);
}
mat2 gc_transpose(mat2 m) { return mat2(m[0][0], m[1][0], m[0][1], m[1][1]); }
)";

// GLSL_PRELUDE de gl.js → SkSL: fuera la cabecera de GLSL ES 3, `#define` como constantes y la
// textura como shader hijo (Skia la muestrea en px, de ahí uSrcSize).
std::string prelude_to_sksl(const std::string& glsl, std::optional<std::string>& error) {
    std::istringstream in(glsl);
    std::string out = "uniform vec2 uSrcSize;\n";
    std::string line;
    int sampler = 0;
    int texture = 0;
    while (std::getline(in, line)) {
        if (line.starts_with("#version") || line.starts_with("precision ") || line == "in vec2 vUv;" ||
            line == "out vec4 outColor;") {
            continue;
        }
        if (line == "uniform sampler2D uSrc;") {
            out += "uniform shader uSrc;\n";
            out += kBilinear;
            ++sampler;
            continue;
        }
        if (line.starts_with("#define ")) {
            std::istringstream def(line.substr(8));
            std::string name;
            std::string value;
            def >> name >> value;
            out += std::format("const float {} = {};\n", name, value);
            continue;
        }
        if (const auto at = line.find("return texture(uSrc, t);"); at != std::string::npos) {
            line.replace(at, 24, "return gc_bilinear(t);");
            ++texture;
        }
        out += line + "\n";
    }
    if (sampler != 1 || texture != 1) {
        error = "el preludio GLSL de los efectos cambió (vfx_shaders.inc): hay que revisar su traducción a SkSL";
    }
    return out;
}

const ShaderTable& shader_table() {
    static const ShaderTable table = [] {
        ShaderTable t;
        const Json prelude = Json::parse(joined(kVfxPreludeJson));
        t.prelude_sksl = prelude_to_sksl(prelude.get<std::string>(), t.error);
        for (Json& e : Json::parse(joined(kVfxShadersJson))) {
            t.effects.emplace(e["id"].get<std::string>(), std::move(e));
        }
        return t;
    }();
    return table;
}

// MAIN de gl.js: uv del contenido y el resultado acotado a 0–1.
constexpr std::string_view kMain = R"(
half4 main(float2 p) {
  vec2 vUv = p / uRes;
  vec2 uv = (vUv - uContent.xy) / (uContent.zw - uContent.xy);
  return half4(clamp(effect(uv), 0.0, 1.0));
}
)";

Result<std::string> sksl_of(std::string_view id) {
    const ShaderTable& t = shader_table();
    if (t.error) {
        return fail(ErrorCode::Internal, *t.error);
    }
    const auto it = t.effects.find(id);
    if (it == t.effects.end()) {
        return fail(ErrorCode::Unsupported, std::format("efecto de vídeo sin shader: {}", id));
    }
    std::string frag = it->second["frag"].get<std::string>();
    for (std::size_t at = frag.find("transpose("); at != std::string::npos; at = frag.find("transpose(", at + 13)) {
        frag.replace(at, 10, "gc_transpose(");
    }
    return t.prelude_sksl + frag + std::string(kMain);
}

Result<sk_sp<SkRuntimeEffect>> runtime_effect(std::string_view id) {
    static std::mutex mutex;
    static std::map<std::string, sk_sp<SkRuntimeEffect>, std::less<>> cache;
    const std::scoped_lock lock(mutex);
    if (const auto it = cache.find(id); it != cache.end()) {
        return it->second;
    }
    auto sksl = sksl_of(id);
    if (!sksl) {
        return std::unexpected(sksl.error());
    }
    auto made = SkRuntimeEffect::MakeForShader(SkString(sksl->c_str()));
    if (!made.effect) {
        return fail(ErrorCode::Internal, std::format("el SkSL del efecto {} no compila: {}", id, made.errorText.c_str()));
    }
    cache.emplace(std::string(id), made.effect);
    return made.effect;
}

// --- Imágenes ----------------------------------------------------------------------------------

// La fuente como la sube WebGL (UNPACK_PREMULTIPLY_ALPHA): premultiplicada en 8 bits, con el
// redondeo de Skia (SkMulDiv255Round), que es como Chromium guarda la imagen decodificada.
sk_sp<SkImage> premultiplied(const Image& img) {
    std::vector<std::uint8_t> px(img.pixels.size());
    for (std::size_t i = 0; i + 3 < img.pixels.size(); i += 4) {
        const unsigned a = img.pixels[i + 3];
        for (std::size_t c = 0; c < 3; ++c) {
            const unsigned prod = (img.pixels[i + c] * a) + 128;
            px[i + c] = static_cast<std::uint8_t>((prod + (prod >> 8U)) >> 8U);
        }
        px[i + 3] = static_cast<std::uint8_t>(a);
    }
    const SkImageInfo info = SkImageInfo::Make(img.width, img.height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    const auto row = static_cast<std::size_t>(img.width) * 4;
    return SkImages::RasterFromData(info, SkData::MakeWithCopy(px.data(), px.size()), row);
}

Image unpremultiplied(SkSurface& surface) {
    Image out;
    out.width = surface.width();
    out.height = surface.height();
    out.pixels.resize(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4);
    const SkImageInfo info =
        SkImageInfo::Make(out.width, out.height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
    (void)surface.readPixels(info, out.pixels.data(), static_cast<std::size_t>(out.width) * 4, 0, 0);
    return out;
}

double num(const Json& v, double fallback) {
    if (v.is_number()) {
        const double d = v.get<double>();
        return std::isfinite(d) ? d : fallback;
    }
    return fallback;
}

// uniformValue de params.js para un parámetro ya resuelto.
void set_param_uniform(SkRuntimeShaderBuilder& b, const SkRuntimeEffect& fx, const Json& spec, const Json& value) {
    const std::string name = "u_" + spec["id"].get<std::string>();
    if (fx.findUniform(name) == nullptr) {
        return;
    }
    const std::string type = spec["type"].get<std::string>();
    if (type == "color") { // hexToRgb01
        const std::string hex = value.is_string() ? value.get<std::string>() : "#ffffff";
        const unsigned long n = std::stoul(hex.substr(1), nullptr, 16);
        b.uniform(name) = SkV3{static_cast<float>((n >> 16U) & 255U) / 255.0F,
                                       static_cast<float>((n >> 8U) & 255U) / 255.0F,
                                       static_cast<float>(n & 255U) / 255.0F};
        return;
    }
    float f = 0.0F;
    if (type == "toggle") {
        f = value.is_boolean() && value.get<bool>() ? 1.0F : 0.0F;
    } else if (type == "select") {
        const Json& opts = spec["options"];
        for (std::size_t i = 0; i < opts.size(); ++i) {
            if ((opts[i].is_object() ? opts[i].value("id", Json()) : opts[i]) == value) {
                f = static_cast<float>(i);
                break;
            }
        }
    } else {
        f = static_cast<float>(num(value, 0.0)); // Number(v) || 0
    }
    b.uniform(name) = f;
}

struct Stage {
    sk_sp<SkImage> input;
    std::array<double, 4> src_rect{0, 0, 1, 1};
};

// drawContent: la fuente en su sitio del lienzo, sin efecto.
void draw_content(SkCanvas& g, const Stage& in, const VfxLayout& L) {
    const double sw = in.input->width();
    const double sh = in.input->height();
    const SkRect src = SkRect::MakeLTRB(static_cast<float>(in.src_rect[0] * sw), static_cast<float>(in.src_rect[1] * sh),
                                        static_cast<float>(in.src_rect[2] * sw), static_cast<float>(in.src_rect[3] * sh));
    const SkRect dst = SkRect::MakeXYWH(static_cast<float>(L.pad), static_cast<float>(L.pad),
                                        static_cast<float>(L.cw0), static_cast<float>(L.ch0));
    g.drawImageRect(in.input, src, dst, SkSamplingOptions(SkFilterMode::kLinear), nullptr,
                    SkCanvas::kStrict_SrcRectConstraint);
}

} // namespace

VfxLayout vfx_layout(int w, int h, double pad_frac, double max_side) {
    VfxLayout L;
    L.k = std::min(1.0, max_side / std::max({1.0, static_cast<double>(w), static_cast<double>(h)}));
    L.cw0 = std::max(2, static_cast<int>(std::lround(w * L.k)));
    L.ch0 = std::max(2, static_cast<int>(std::lround(h * L.k)));
    L.pad = pad_frac > 0 ? std::max(1, static_cast<int>(std::floor((pad_frac * std::min(L.cw0, L.ch0)) + 0.5))) : 0;
    L.cw = L.cw0 + (2 * L.pad);
    L.ch = L.ch0 + (2 * L.pad);
    L.content = {static_cast<double>(L.pad) / L.cw, static_cast<double>(L.pad) / L.ch,
                 static_cast<double>(L.pad + L.cw0) / L.cw, static_cast<double>(L.pad + L.ch0) / L.ch};
    L.pad_src = L.k > 0 ? L.pad / L.k : 0.0;
    return L;
}

double vfx_pad_frac(const Json& entries) {
    double p = 0.0;
    const auto cat = core::fx_catalog();
    for (const Json& e : entries) {
        if (const Json* m = cat ? cat->effect(e["effect"]) : nullptr) {
            p += num(m->value("pad", Json(0)), 0.0);
        }
    }
    return std::min(0.5, p);
}

Json clip_vfx(const Json& vfx) {
    Json out = Json::array();
    if (!vfx.is_array()) {
        return out;
    }
    const auto cat = core::fx_catalog();
    core::SequentialIdSource ids;
    for (const Json& raw : vfx) {
        if (raw.is_null() || (raw.is_object() && raw.value("enabled", Json(true)) == Json(false))) {
            continue;
        }
        auto e = core::normalize_vfx_entry(raw, ids);
        const Json* m = e && cat ? cat->effect((*e)["effect"]) : nullptr;
        if (m != nullptr && (*m)["engine"] != "generator") {
            out.push_back(std::move(*e));
        }
    }
    return out;
}

Result<std::string> vfx_sksl(std::string_view effect_id) { return sksl_of(effect_id); }

void set_vfx_assets_dir(std::filesystem::path dir) {
    const std::scoped_lock lock(assets_mutex());
    assets_dir() = std::move(dir);
}

Result<std::optional<VfxBakePlan>> plan_vfx_bake(const core::TimelineClip& clip, int iw, int ih, int W, int H,
                                                 int fps) {
    auto has = core::has_vfx(clip);
    if (!has) {
        return std::unexpected(has.error());
    }
    if (!*has) {
        return std::optional<VfxBakePlan>{};
    }
    VfxBakePlan plan;
    auto entries = core::active_vfx(clip);
    if (!entries) {
        return std::unexpected(entries.error());
    }
    plan.entries = std::move(*entries);
    // Solo los de motor «shader»: el resto (paper_cutout, de Canvas 2D) aún no, y mejor saberlo antes
    // de empezar a codificar.
    const auto cat = core::fx_catalog();
    for (const Json& e : plan.entries) {
        const Json* man = cat ? cat->effect(e["effect"]) : nullptr;
        if (man == nullptr || !drawable(*man)) {
            return fail(ErrorCode::Unsupported,
                        std::format("el render aún no aplica el efecto de vídeo {} (clip {})", e["effect"].dump(), clip.id));
        }
    }
    plan.pad = core::vfx_pad_fraction(plan.entries) > 0 && core::vfx_pad_allowed(clip, iw, ih, W, H);
    plan.dur = std::max(0.05, core::clip_timeline_duration(clip));
    // int(round(dur * fps)) de Python: al par en los medios exactos (0,35 s a 30 fps → 10).
    plan.frames = std::max<std::int64_t>(1, static_cast<std::int64_t>(std::nearbyint(plan.dur * fps)));
    plan.baked = core::baked_clip(clip, clip.filename);
    plan.source = clip;
    return std::optional<VfxBakePlan>(std::move(plan));
}

std::pair<int, int> vfx_baked_size(const VfxBakePlan& plan, int iw, int ih) {
    const int w = iw - (iw % 2); // crop=trunc(iw/2)*2:trunc(ih/2)*2
    const int h = ih - (ih % 2);
    const VfxLayout L = vfx_layout(w, h, plan.pad ? vfx_pad_frac(plan.entries) : 0.0, 1e300);
    return {L.cw, L.ch};
}

std::int64_t vfx_baked_index(const VfxBakePlan& plan, std::int64_t tick, int fps) {
    // Las setpts del export: pts = trunc(m + inicio/TB), con el inicio a 3 decimales (clip_chain de
    // gc_media). El trim del clip horneado adelanta el final (ffmpeg_trim_window): quedan los
    // fotogramas con m/F antes de la salida.
    const std::string start_text = std::format("{:.3f}", std::max(0.0, plan.baked.start));
    double start = 0.0;
    (void)std::from_chars(start_text.data(), start_text.data() + start_text.size(), start);
    const double offset = start / (1.0 / static_cast<double>(fps));
    const double out = core::ffmpeg_trim_window(plan.baked, fps).second;
    std::int64_t n = 0;
    while (n < plan.frames && static_cast<double>(n) / fps < out) {
        ++n;
    }
    n = std::max<std::int64_t>(n, 1);
    std::int64_t chosen = 0;
    for (std::int64_t m = 0; m < n; ++m) {
        if (static_cast<std::int64_t>(static_cast<double>(m) + offset) <= tick) {
            chosen = m;
        } else {
            break;
        }
    }
    return chosen;
}

Result<Image> vfx_baked_frame(const VfxBakePlan& plan, const Image& source_frame, std::int64_t m, int fps) {
    Image even;
    even.width = source_frame.width - (source_frame.width % 2);
    even.height = source_frame.height - (source_frame.height % 2);
    if (even.width <= 0 || even.height <= 0) {
        return fail(ErrorCode::InvalidArgument, "vfx_baked_frame: el material no tiene tamaño");
    }
    even.pixels.resize(static_cast<std::size_t>(even.width) * static_cast<std::size_t>(even.height) * 4);
    const auto src_row = static_cast<std::size_t>(source_frame.width) * 4;
    const auto dst_row = static_cast<std::size_t>(even.width) * 4;
    for (int y = 0; y < even.height; ++y) {
        std::copy_n(source_frame.pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * src_row),
                    dst_row, even.pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * dst_row));
    }
    auto out = render_vfx(even, plan.entries, static_cast<double>(m) / fps, plan.dur, plan.pad, 1e300);
    if (!out) {
        return std::unexpected(out.error());
    }
    return std::move(out->image);
}

Result<VfxFrame> render_vfx(const Image& source, const Json& entries, double t, double clip_dur, bool pad_allowed,
                            double max_side) {
    const int w = source.width;
    const int h = source.height;
    if (w <= 0 || h <= 0 || source.pixels.size() != static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4) {
        return fail(ErrorCode::InvalidArgument, "render_vfx: la fuente no es una imagen RGBA válida");
    }
    const auto cat = core::fx_catalog();
    const double pad_frac = pad_allowed ? vfx_pad_frac(entries) : 0.0;
    // vfxActiveAt: los efectos cuya ventana [from, to) contiene t.
    std::vector<const Json*> active;
    for (const Json& e : entries) {
        const double to = e["to"].is_null() ? clip_dur : std::min(clip_dur, e["to"].get<double>());
        const double from = std::min(e["from"].get<double>(), clip_dur);
        if (t >= from && t < to + 1e-6) {
            active.push_back(&e);
        }
    }
    const VfxLayout L = vfx_layout(w, h, pad_frac, max_side);
    const SkImageInfo info = SkImageInfo::Make(L.cw, L.ch, kRGBA_8888_SkColorType, kPremul_SkAlphaType);

    Stage stage{premultiplied(source), {0, 0, 1, 1}};
    sk_sp<SkSurface> out;
    const std::size_t n = active.empty() ? 1 : active.size();
    for (std::size_t i = 0; i < n; ++i) {
        out = SkSurfaces::Raster(info);
        if (!out) {
            return fail(ErrorCode::Internal, "render_vfx: Skia no pudo reservar el lienzo");
        }
        SkCanvas& g = *out->getCanvas();
        g.clear(SK_ColorTRANSPARENT);
        if (active.empty()) {
            draw_content(g, stage, L);
        } else {
            const Json& entry = *active[i];
            const std::string id = entry["effect"].get<std::string>();
            const Json* manifest = cat ? cat->effect(entry["effect"]) : nullptr;
            if (manifest == nullptr) {
                return fail(ErrorCode::NotFound, std::format("efecto desconocido: {}", id));
            }
            auto params = core::FxCatalog::resolve_params(*manifest, entry["params"]);
            if (!params) {
                return std::unexpected(params.error());
            }
            if (manifest->value("engine", Json()) == Json("canvas")) {
                if (!drawable(*manifest)) {
                    return fail(ErrorCode::Unsupported, std::format("el render aún no dibuja el efecto {}", id));
                }
                // effect.render de paper.js sobre el lienzo, con el contenido en su sitio.
                const double to = entry["to"].is_null() ? clip_dur : std::min(clip_dur, entry["to"].get<double>());
                const double from = std::min(entry["from"].get<double>(), clip_dur);
                const double time = std::max(0.0, t - from);
                const double dur = std::max(1e-3, to - from);
                const double seed_raw = num(params->value("seed", Json(1)), 1.0);
                std::filesystem::path assets;
                {
                    const std::scoped_lock lock(assets_mutex());
                    assets = assets_dir();
                }
                const auto sw = static_cast<float>(stage.input->width());
                const auto sh = static_cast<float>(stage.input->height());
                const SkRect src = SkRect::MakeLTRB(static_cast<float>(stage.src_rect[0]) * sw,
                                                    static_cast<float>(stage.src_rect[1]) * sh,
                                                    static_cast<float>(stage.src_rect[2]) * sw,
                                                    static_cast<float>(stage.src_rect[3]) * sh);
                const SkRect content = SkRect::MakeXYWH(static_cast<float>(L.pad), static_cast<float>(L.pad),
                                                        static_cast<float>(L.cw0), static_cast<float>(L.ch0));
                if (auto ok = detail::paper_cutout(g, stage.input, src, content, time, dur, *params,
                                                   seed_raw != 0.0 ? seed_raw : 1.0, assets);
                    !ok) {
                    return std::unexpected(ok.error());
                }
                stage = Stage{out->makeImageSnapshot(), L.content};
                continue;
            }
            auto fx = runtime_effect(id);
            if (!fx) {
                return std::unexpected(fx.error());
            }
            const double to = entry["to"].is_null() ? clip_dur : std::min(clip_dur, entry["to"].get<double>());
            const double from = std::min(entry["from"].get<double>(), clip_dur);
            const double time = std::max(0.0, t - from);
            const double dur = std::max(1e-3, to - from);
            const double frame = std::floor((time * kFxFps) + 1e-6);
            const double seed_raw = num(params->value("seed", Json(1)), 1.0);
            const double seed = seed_raw != 0.0 ? seed_raw : 1.0; // Number(seed ?? 1) || 1

            SkRuntimeShaderBuilder b(*fx);
            const auto set = [&](const char* name, auto value) {
                if ((*fx)->findUniform(name) != nullptr) {
                    b.uniform(name) = value;
                }
            };
            set("uSrcSize", SkV2{static_cast<float>(stage.input->width()), static_cast<float>(stage.input->height())});
            set("uSrcRect", SkV4{static_cast<float>(stage.src_rect[0]), static_cast<float>(stage.src_rect[1]),
                                 static_cast<float>(stage.src_rect[2]), static_cast<float>(stage.src_rect[3])});
            set("uRes", SkV2{static_cast<float>(L.cw), static_cast<float>(L.ch)});
            set("uContent", SkV4{static_cast<float>(L.content[0]), static_cast<float>(L.content[1]),
                                 static_cast<float>(L.content[2]), static_cast<float>(L.content[3])});
            set("uTime", static_cast<float>(time));
            set("uDur", static_cast<float>(dur));
            set("uProgress", static_cast<float>(dur > 0 ? std::min(1.0, std::max(0.0, time / dur)) : 0.0));
            set("uFrame", static_cast<float>(frame));
            set("uSeed", static_cast<float>(seed));
            for (const Json& spec : (*manifest)["params"]) {
                set_param_uniform(b, **fx, spec, (*params)[spec["id"].get<std::string>()]);
            }
            b.child("uSrc") = stage.input->makeShader(SkTileMode::kClamp, SkTileMode::kClamp,
                                                      SkSamplingOptions(SkFilterMode::kNearest));
            SkPaint paint;
            paint.setShader(b.makeShader());
            paint.setBlendMode(SkBlendMode::kSrc);
            g.drawRect(SkRect::MakeIWH(L.cw, L.ch), paint);
        }
        stage = Stage{out->makeImageSnapshot(), L.content};
    }
    return VfxFrame{unpremultiplied(*out), L};
}

} // namespace gc::render
