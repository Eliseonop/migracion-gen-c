#include "gc/core/bg_matte.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <system_error>
#include <utility>

#include "gc/core/bg.hpp"
#include "gc/core/export_input.hpp"
#include "gc/core/io.hpp"
#include "gc/core/timeline_rules.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

namespace {

namespace fs = std::filesystem;
using detail::py_float;
using detail::py_int;
using detail::py_round;
using detail::py_truthy;

using Range = std::pair<std::int64_t, std::int64_t>;

const Json& field(const Json& obj, std::string_view key) {
    static const Json kNull;
    if (!obj.is_object()) {
        return kNull;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? kNull : *it;
}

// read_meta: el meta.json si es un objeto JSON; nada si no está, no se lee o no lo es.
std::optional<Json> read_meta(const fs::path& folder) {
    std::error_code ec;
    if (!fs::exists(folder / "meta.json", ec)) {
        return std::nullopt;
    }
    auto data = read_json_file_py(folder / "meta.json");
    if (!data || !data->is_object()) {
        return std::nullopt;
    }
    return std::move(*data);
}

// covered_range: el «range» del meta (una lista de dos, con el int() de Python); nada si no lo hay.
Result<std::optional<Range>> covered_range(const Json& meta) {
    const Json& rng = field(meta, "range");
    if (!rng.is_array() || rng.size() != 2) {
        return std::optional<Range>{};
    }
    const auto lo = py_int(rng[0]);
    const auto hi = py_int(rng[1]);
    if (!lo || !hi) {
        return fail(ErrorCode::InvalidArgument, "ValueError: el rango del meta.json del matte no son enteros");
    }
    return std::optional<Range>(Range{*lo, *hi});
}

// int(meta.get(key) or 0) / float(meta.get(key) or 0.0): lo que falta o es falso vale 0.
Result<std::int64_t> meta_int(const Json& meta, std::string_view key, std::int64_t fallback) {
    const Json& v = field(meta, key);
    if (!py_truthy(v)) {
        return fallback;
    }
    if (const auto n = py_int(v)) {
        return *n;
    }
    return fail(ErrorCode::InvalidArgument, std::format("ValueError: int() de «{}» del meta.json del matte", key));
}

Result<double> meta_float(const Json& meta, std::string_view key) {
    const Json& v = field(meta, key);
    if (!py_truthy(v)) {
        return 0.0;
    }
    if (const auto n = py_float(v)) {
        return *n;
    }
    return fail(ErrorCode::InvalidArgument, std::format("ValueError: float() de «{}» del meta.json del matte", key));
}

double clamp01(double v) {
    return std::min(1.0, std::max(0.0, v)); // _clamp de clip_bg
}

} // namespace

std::int64_t matte_frame_index(double src_t, std::int64_t mask_fps) {
    // fps = max(MASK_FPS_MIN, int(mask_fps or DEFAULT_MASK_FPS)); max(0, int(floor(max(0, _num(t)) · fps + 0,5)))
    const auto fps = static_cast<double>(std::max<std::int64_t>(1, mask_fps != 0 ? mask_fps : 15));
    const double t = std::isfinite(src_t) ? src_t : 0.0;
    const double v = std::floor((std::max(0.0, t) * fps) + 0.5);
    if (!(v < 9.0e18)) {
        return std::numeric_limits<std::int64_t>::max(); // Python seguiría con un entero enorme
    }
    return std::max<std::int64_t>(0, static_cast<std::int64_t>(v));
}

std::string matte_frame_name(std::int64_t index) {
    return std::format("{:06d}.png", index + 1);
}

std::string matte_source_chain(const Json& normalized_bg, std::int64_t iw, std::int64_t ih, std::int64_t outline_height,
                               int fps) {
    const std::string matte =
        std::format("[matte]format=gray,scale={}:{}:flags=bicubic,fps={},setpts=PTS-STARTPTS[bgm]", iw, ih, fps);
    // `fit`: el material al tamaño sondeado (alphamerge y blend exigen el mismo tamaño).
    std::string out;
    if (bg_chroma_active(normalized_bg)) {
        const Json& chroma = normalized_bg.at("chroma");
        std::string keyed;
        for (const auto& f : chroma_filters(chroma)) {
            keyed += (keyed.empty() ? "" : ",") + f;
        }
        const std::string amorph = chroma_alpha_ffmpeg(chroma, ih);
        out = std::format(",scale={}:{},{},format=gbrap,fps={}[bgk];{};[bgk]split=2[bgka][bgkb];"
                          "[bgka]alphaextract{}[bgkalpha];[bgkalpha][bgm]blend=all_mode=multiply,format=gray[bgmix];"
                          "[bgkb][bgmix]alphamerge",
                          iw, ih, keyed, fps, matte, amorph.empty() ? "" : "," + amorph);
    } else {
        out = std::format(",scale={}:{},format=gbrap,fps={}[bgs];{};[bgs][bgm]alphamerge", iw, ih, fps, matte);
    }
    if (const std::string outline = outline_source_chain(field(normalized_bg, "outline"), outline_height); !outline.empty()) {
        out += "," + outline;
    }
    return out;
}

std::array<std::uint8_t, 256> matte_lut(const Json& normalized_auto) {
    const bool invert = py_truthy(field(normalized_auto, "invert"));
    const double threshold = py_float(field(normalized_auto, "threshold")).value_or(0.5);
    const double softness = py_float(field(normalized_auto, "softness")).value_or(0.25);
    const double half = std::max(0.002, softness * 0.5);
    const double lo = threshold - half;
    const double span = (threshold + half) - lo;
    std::array<std::uint8_t, 256> lut{};
    for (std::size_t i = 0; i < lut.size(); ++i) {
        double v = clamp01(((static_cast<double>(i) / 255.0) - lo) / span);
        if (invert) {
            v = 1.0 - v;
        }
        lut[i] = static_cast<std::uint8_t>(std::min(255.0, std::floor((v * 255.0) + 0.5)));
    }
    return lut;
}

Result<std::optional<MatteSpec>> clip_matte_spec(const TimelineClip& clip, const std::filesystem::path& cache_root) {
    if (!clip.bg_removal || !bg_capable(clip)) {
        return std::optional<MatteSpec>{};
    }
    const Json bg = normalize_bg(*clip.bg_removal);
    if (!bg_auto_active(bg)) {
        return std::optional<MatteSpec>{};
    }
    const Json& a = bg.at("auto");
    MatteSpec spec;
    spec.folder = cache_root / "matte" / path_from_utf8(a.at("base_key").get<std::string>());
    const auto meta = read_meta(spec.folder);
    if (!meta || meta->empty()) { // `if not meta`: tampoco un objeto vacío
        return std::optional<MatteSpec>{};
    }
    auto fps = meta_int(*meta, "mask_fps", a.at("mask_fps").get<std::int64_t>());
    if (!fps) {
        return std::unexpected(fps.error());
    }
    spec.mask_fps = *fps;
    const double inp = clip.in_point;
    const double src_dur = std::max(0.04, clip_source_duration(clip));
    const bool still = is_still_clip(clip);
    std::int64_t i0 = 0;
    std::int64_t i1 = 0;
    if (still) {
        auto rng = covered_range(*meta);
        if (!rng) {
            return std::unexpected(rng.error());
        }
        i1 = std::max<std::int64_t>(0, *rng ? (*rng)->second : 0);
        auto loop_dur = meta_float(*meta, "source_duration");
        if (!loop_dur) {
            return std::unexpected(loop_dur.error());
        }
        if (*loop_dur > 0) {
            i1 = std::min(i1, matte_frame_index(*loop_dur, spec.mask_fps));
        }
    } else {
        i0 = matte_frame_index(inp, spec.mask_fps);
        i1 = matte_frame_index(inp + src_dur, spec.mask_fps);
    }

    // ensure_derived con el nivel 2 vacío: el tramo [lo, hi] sale de su crudo (si existe), y lo
    // de fuera, de una copia del derivado del extremo, que tiene que existir.
    auto rng = covered_range(*meta);
    if (!rng) {
        return std::unexpected(rng.error());
    }
    if (!*rng) {
        return std::optional<MatteSpec>{};
    }
    const std::int64_t lo = std::max((*rng)->first, i0);
    const std::int64_t hi = std::min((*rng)->second, std::max(i1, i0));
    std::error_code ec;
    const auto raw = [&](std::int64_t idx) { return fs::exists(spec.folder / matte_frame_name(idx), ec); };
    const bool has_lo = lo <= hi && raw(lo);
    const bool has_hi = lo <= hi && raw(hi);
    if ((i0 < lo && !has_lo) || (hi < i1 && !has_hi)) {
        return fail(ErrorCode::NotFound, "FileNotFoundError: el clip cae fuera de lo que se procesó del matte",
                    path_to_utf8(spec.folder));
    }
    spec.start_number = i0 + 1;
    spec.frames = i1 - i0 + 1;
    spec.sources.reserve(static_cast<std::size_t>(std::max<std::int64_t>(0, spec.frames)));
    for (std::int64_t idx = i0; idx <= i1; ++idx) {
        if (idx < lo) {
            spec.sources.emplace_back(lo);
        } else if (idx > hi) {
            spec.sources.emplace_back(hi);
        } else if (raw(idx)) {
            spec.sources.emplace_back(idx);
        } else {
            spec.sources.emplace_back(std::nullopt);
        }
    }

    auto loop_dur = meta_float(*meta, "source_duration");
    if (!loop_dur) {
        return std::unexpected(loop_dur.error());
    }
    spec.loop = still && *loop_dur > 0.0 && spec.frames > 1;
    spec.duration = py_round(src_dur, 3);
    auto w = meta_int(*meta, "width", 0);
    if (!w) {
        return std::unexpected(w.error());
    }
    auto h = meta_int(*meta, "height", 0);
    if (!h) {
        return std::unexpected(h.error());
    }
    spec.width = *w;
    spec.height = *h;
    // El atajo de derive_matte: sin pluma, expansión, opacidad ni pincel (en SAM los puntos son el
    // prompt, no una corrección), el derivado es la LUT.
    const double feather = py_float(a.at("feather")).value_or(0.0);
    const double expansion = py_float(a.at("expansion")).value_or(0.0);
    const double opacity = py_float(a.at("opacity")).value_or(1.0);
    const Json& provider = a.at("provider");
    const bool interactive = provider.is_string() && provider.get_ref<const std::string&>().starts_with("sam");
    spec.lut_only = feather <= 0.0 && std::abs(expansion) < 1e-4 && opacity >= 1.0 &&
                    (interactive || a.at("edits").empty());
    spec.lut = matte_lut(a);
    spec.settings = a;
    return std::optional<MatteSpec>(std::move(spec));
}

} // namespace gc::core
