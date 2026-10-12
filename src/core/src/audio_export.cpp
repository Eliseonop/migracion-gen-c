#include "gc/core/audio_export.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <map>
#include <set>
#include <utility>

#include "gc/core/export_input.hpp"
#include "gc/core/keyframes.hpp"
#include "gc/core/timeline_rules.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

namespace {

using detail::py_float;
using detail::py_num;
using detail::py_round;

const Json& field(const Json& obj, std::string_view key) {
    static const Json kNull;
    if (!obj.is_object()) {
        return kNull;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? kNull : *it;
}

// Los items de keyframes que son objetos (`_items` de clip_keyframes.py con el filtro de dicts).
std::vector<const Json*> kf_items(const TimelineClip& clip) {
    std::vector<const Json*> out;
    if (!clip.keyframes || !clip.keyframes->is_object()) {
        return out;
    }
    const Json& items = field(*clip.keyframes, "items");
    if (items.is_array()) {
        for (const Json& k : items) {
            if (k.is_object()) {
                out.push_back(&k);
            }
        }
    }
    return out;
}

// min(1\,max(0\,(t-ta)/span)) de _u_expr; span = max(tb − ta, 1e-6) como el max de Python.
std::string u_expr(double ta, double tb) {
    const double d = tb - ta;
    const double span = 1e-6 > d ? 1e-6 : d;
    return std::format("min(1\\,max(0\\,(t-{:.6f})/{:.6f}))", ta, span);
}

std::string eased_expr(const std::string& u, Interp k) {
    switch (k) {
    case Interp::Hold: return "0";
    case Interp::EaseIn: return std::format("(({0})*({0}))", u);
    case Interp::EaseOut: return std::format("(1-(1-({0}))*(1-({0})))", u);
    case Interp::EaseInOut: return std::format("if(lt({0}\\,0.5)\\,2*({0})*({0})\\,1-2*(1-({0}))*(1-({0})))", u);
    case Interp::CubicIn: return std::format("(({0})*({0})*({0}))", u);
    case Interp::CubicOut: return std::format("(1-(1-({0}))*(1-({0}))*(1-({0})))", u);
    case Interp::CubicInOut:
        return std::format("if(lt({0}\\,0.5)\\,4*({0})*({0})*({0})\\,1-(2-2*({0}))*(2-2*({0}))*(2-2*({0}))/2)", u);
    default: return std::format("({})", u);
    }
}

std::string lerp_expr(double va, double vb, double ta, double tb, const Json& interp) {
    const std::string eu = eased_expr(u_expr(ta, tb), normalize_interp(interp));
    return std::format("({:.6f}+(({:.6f})-({:.6f}))*{})", va, vb, va, eu);
}

// _segment_steps: un tramo recto por fotograma a 30 fps, entre 2 y 120. (Python lanza
// OverflowError con un tramo infinito; aquí son 120.)
int segment_steps(double span) {
    const double steps = std::ceil(std::max(0.0, span) / (1.0 / 30.0));
    if (!std::isfinite(steps)) {
        return 120;
    }
    return static_cast<int>(std::min(120.0, std::max(2.0, steps)));
}

// --- Catálogo de efectos ---------------------------------------------------------------------

using P = std::pair<std::string_view, std::string_view>;

AudioNode node(std::string_view type, std::initializer_list<P> params) { return {type, params}; }

std::vector<AudioFxDef> make_catalog() {
    constexpr std::string_view kBw = "0.7071";
    return {
        {"eq", "Brillo (EQ)", "effect", {{node("peaking", {{"f", "3000"}, {"q", "1"}, {"g", "4"}})}}},
        {"compressor", "Compresor", "effect",
         {{node("compressor",
                {{"threshold", "-20"}, {"ratio", "8"}, {"attack", "0.02"}, {"release", "0.2"}, {"knee", "6"}})}}},
        {"reverb", "Reverberación", "effect",
         {{node("echo", {{"in", "0.8"}, {"out", "0.88"}, {"delay", "0.04"}, {"decay", "0.4"}})}}},
        {"echo", "Eco", "effect", {{node("echo", {{"in", "0.8"}, {"out", "0.9"}, {"delay", "1.0"}, {"decay", "0.3"}})}}},
        {"denoise", "Reducir ruido", "effect",
         {{node("highpass", {{"f", "80"}, {"q", kBw}}), node("lowpass", {{"f", "12000"}, {"q", kBw}})}}},
        {"distortion", "Distorsión", "effect", {{node("drive", {{"k", "4"}})}}},
        {"underwater", "Bajo el agua", "filter",
         {
             {node("lowpass", {{"f", "2500"}, {"q", "0.8"}}), node("lowshelf", {{"f", "160"}, {"g", "2"}})},
             {node("lowpass", {{"f", "900"}, {"q", "0.85"}}), node("lowshelf", {{"f", "160"}, {"g", "3.5"}})},
             {node("lowpass", {{"f", "380"}, {"q", "0.9"}}), node("lowshelf", {{"f", "160"}, {"g", "5"}})},
         }},
        {"telephone", "Teléfono", "filter",
         {{node("highpass", {{"f", "450"}, {"q", kBw}}), node("lowpass", {{"f", "3000"}, {"q", kBw}}),
           node("peaking", {{"f", "1700"}, {"q", "1"}, {"g", "6"}})}}},
        {"radio", "Radio antigua", "filter",
         {{node("highpass", {{"f", "300"}, {"q", kBw}}), node("lowpass", {{"f", "4500"}, {"q", kBw}}),
           node("peaking", {{"f", "1200"}, {"q", "0.8"}, {"g", "5"}}), node("drive", {{"k", "1.5"}})}}},
        {"megaphone", "Megáfono", "filter",
         {{node("highpass", {{"f", "800"}, {"q", kBw}}), node("lowpass", {{"f", "3200"}, {"q", kBw}}),
           node("peaking", {{"f", "2000"}, {"q", "1.2"}, {"g", "9"}}), node("drive", {{"k", "3"}})}}},
        {"muffled", "Amortiguado", "filter",
         {
             {node("lowpass", {{"f", "3500"}, {"q", kBw}})},
             {node("lowpass", {{"f", "1600"}, {"q", kBw}})},
             {node("lowpass", {{"f", "900"}, {"q", kBw}})},
         }},
    };
}

std::string_view param(const AudioNode& n, std::string_view key) {
    for (const auto& [k, v] : n.params) {
        if (k == key) {
            return v;
        }
    }
    return {};
}

double number(const AudioNode& n, std::string_view key) {
    const std::string_view text = param(n, key);
    double v = 0.0;
    std::from_chars(text.data(), text.data() + text.size(), v);
    return v;
}

// Las intensidades sin animar: las de clip_props_at(0) con keyframes; si no, las de audio_fx.
std::vector<double> static_values(const TimelineClip& clip) {
    std::vector<double> out;
    const bool kf = keyframes_enabled(clip);
    const KfProps props = kf ? clip_props_at(clip, 0.0) : KfProps{};
    const Json raw = clip.audio_fx && clip.audio_fx->is_object() ? *clip.audio_fx : Json::object();
    for (const AudioFxDef& fx : audio_fx_catalog()) {
        out.push_back(kf ? fx_intensity(Json(props.get(fx.id))) : fx_intensity(field(raw, fx.id)));
    }
    return out;
}

} // namespace

// --- Envolventes ---------------------------------------------------------------------------------

std::optional<std::string> ffmpeg_envelope(const TimelineClip& clip, std::string_view key, double fallback) {
    if (!keyframes_enabled(clip)) {
        return std::nullopt;
    }
    std::vector<const Json*> raw = kf_items(clip);
    std::ranges::stable_sort(raw, {}, [](const Json* k) { return py_num(field(*k, "t"), 0.0); });
    if (raw.size() < 2) {
        return std::nullopt;
    }
    const auto value_at = [&](double t) {
        const double v = clip_props_at(clip, t).get(key);
        if (key == "volume") {
            return clamp_volume(Json(v));
        }
        const double n = std::isnan(v) ? fallback : v;
        return n > 0.0 ? n : 0.0; // max(0.0, n) de Python
    };
    static const Json kLinear = Json{{"interpolation", "linear"}};
    struct Pt {
        double t;
        double v;
        const Json* k;
    };
    std::vector<Pt> pts;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const Json& k = *raw[i];
        const double t = py_num(field(k, "t"), 0.0);
        const Interp interp = normalize_interp(field(k, "interpolation"));
        if (i > 0 && (interp == Interp::BackOut || interp == Interp::Bezier)) {
            // Curva sin fórmula cerrada: tramos rectos cortos que la siguen.
            const double ta = pts.back().t;
            const int n = segment_steps(t - ta);
            for (int j = 1; j < n; ++j) {
                const double tj = ta + (t - ta) * j / n;
                pts.push_back({tj, value_at(tj), &kLinear});
            }
            pts.push_back({t, value_at(t), &kLinear});
            continue;
        }
        pts.push_back({t, value_at(t), &k});
    }
    std::set<double> values;
    for (const Pt& p : pts) {
        values.insert(py_round(p.v, 4) + 0.0); // + 0.0: −0.0 y 0.0 son el mismo, como en un set de Python
    }
    if (values.size() <= 1) {
        return std::nullopt;
    }
    std::string expr = std::format("{:.6f}", pts.back().v);
    for (std::size_t i = pts.size() - 1; i-- > 0;) {
        const Pt& a = pts[i];
        const Pt& b = pts[i + 1];
        const std::string seg = lerp_expr(a.v, b.v, a.t, b.t, field(*b.k, "interpolation"));
        expr = std::format("if(lt(t\\,{:.6f})\\,{}\\,{})", b.t, seg, expr);
    }
    return std::format("if(lte(t\\,{:.6f})\\,{:.6f}\\,{})", pts.front().t, pts.front().v, expr);
}

std::string volume_filter(const TimelineClip& clip) {
    if (auto expr = ffmpeg_envelope(clip, "volume", 1.0)) {
        return std::format("volume='{}':eval=frame", *expr);
    }
    return std::format("volume={:.3f}", clip_volume_at(clip, 0.0));
}

// --- Efectos de sonido -----------------------------------------------------------------------

std::span<const AudioFxDef> audio_fx_catalog() {
    static const std::vector<AudioFxDef> kCatalog = make_catalog();
    return kCatalog;
}

double fx_intensity(const Json& value) {
    if (value.is_boolean()) {
        return value.get<bool>() ? 1.0 : 0.0;
    }
    if (value.is_null()) {
        return 0.0;
    }
    const auto n = py_float(value);
    if (!n || std::isnan(*n)) {
        return 0.0;
    }
    const double lo = *n > 0.0 ? *n : 0.0;
    return lo < 1.0 ? lo : 1.0;
}

std::string ffmpeg_node(const AudioNode& n) {
    const std::string_view t = n.type;
    if (t == "lowpass" || t == "highpass") {
        return std::format("{}=f={}:t=q:w={}", t, param(n, "f"), param(n, "q"));
    }
    if (t == "peaking") {
        return std::format("equalizer=f={}:t=q:w={}:g={}", param(n, "f"), param(n, "q"), param(n, "g"));
    }
    if (t == "lowshelf" || t == "highshelf") {
        return std::format("{}=f={}:t=s:w=1:g={}", t, param(n, "f"), param(n, "g"));
    }
    if (t == "echo") {
        return std::format("aecho={}:{}:{:g}:{}", param(n, "in"), param(n, "out"), number(n, "delay") * 1000,
                           param(n, "decay"));
    }
    if (t == "compressor") {
        const double threshold = number(n, "threshold");
        const double ratio = number(n, "ratio");
        const double thr = std::pow(10.0, threshold / 20);
        // compressor_makeup: la compensación de Web Audio, (1 / ganancia a 0 dBFS)^0,6.
        const double full = std::pow(10.0, threshold * (1 - 1 / ratio) / 20);
        const double makeup = std::pow(full, -0.6);
        return std::format("acompressor=threshold={:.5f}:ratio={}:attack={:g}:release={:g}:knee={:.4f}:makeup={:.4f}",
                           thr, param(n, "ratio"), number(n, "attack") * 1000, number(n, "release") * 1000,
                           std::pow(10.0, number(n, "knee") / 20), makeup);
    }
    // drive
    const double k = number(n, "k");
    const double th = std::tanh(k);
    return std::format("aeval=exprs=tanh({0:g}*val(0))/{1:.6f}|tanh({0:g}*val(1))/{1:.6f}", k, th);
}

std::vector<double> stage_weights(double k, int n_stages) {
    const double lo = k > 0.0 ? k : 0.0;
    const double p = (lo < 1.0 ? lo : 1.0) * n_stages;
    std::vector<double> out;
    for (int j = 0; j <= n_stages; ++j) {
        const double w = 1 - std::abs(p - j);
        out.push_back(w > 0.0 ? w : 0.0);
    }
    return out;
}

AudioFxGraph audio_fx_graph(const TimelineClip& clip, std::string_view in_label, std::string_view tag) {
    AudioFxGraph g{{}, std::string(in_label)};
    const std::vector<double> stat = static_values(clip);
    const bool kf = keyframes_enabled(clip);
    const auto catalog = audio_fx_catalog();
    for (std::size_t n = 0; n < catalog.size(); ++n) {
        const AudioFxDef& fx = catalog[n];
        const std::optional<std::string> env = kf ? ffmpeg_envelope(clip, fx.id, stat[n]) : std::nullopt;
        const double k = stat[n];
        if (!env && k <= 1e-4) {
            continue;
        }
        std::vector<std::string> chains{""};
        for (const auto& stage : fx.stages) {
            std::string c;
            for (const AudioNode& nd : stage) {
                c += (c.empty() ? "" : ",") + ffmpeg_node(nd);
            }
            chains.push_back(std::move(c));
        }
        const int ns = static_cast<int>(fx.stages.size());
        const std::string out = std::format("{}f{}", tag, n);
        std::vector<std::pair<int, std::string>> branches;
        if (!env) {
            const auto weights = stage_weights(k, ns);
            for (int j = 0; j <= ns; ++j) {
                if (weights[static_cast<std::size_t>(j)] > 1e-4) {
                    branches.emplace_back(j, std::format("volume={:.4f}", weights[static_cast<std::size_t>(j)]));
                }
            }
        } else {
            const std::string e = std::format("min(1\\,max(0\\,{}))", *env);
            for (int j = 0; j <= ns; ++j) {
                branches.emplace_back(j, std::format("volume='max(0\\,1-abs(({})*{}-{}))':eval=frame", e, ns, j));
            }
        }
        if (branches.size() == 1 && !env) {
            const std::string& chain = chains[static_cast<std::size_t>(branches[0].first)];
            g.steps.push_back(chain.empty() ? std::format("[{}]anull[{}]", g.out, out)
                                            : std::format("[{}]{}[{}]", g.out, chain, out));
        } else {
            std::string labels;
            std::string mixed;
            for (const auto& [j, vol] : branches) {
                labels += std::format("[{}s{}]", out, j);
                mixed += std::format("[{}m{}]", out, j);
            }
            g.steps.push_back(std::format("[{}]asplit={}{}", g.out, branches.size(), labels));
            for (const auto& [j, vol] : branches) {
                const std::string& chain = chains[static_cast<std::size_t>(j)];
                const std::string body = chain.empty() ? vol : std::format("{},{}", chain, vol);
                g.steps.push_back(std::format("[{}s{}]{}[{}m{}]", out, j, body, out, j));
            }
            g.steps.push_back(std::format("{}amix=inputs={}:normalize=0:duration=first[{}]", mixed, branches.size(), out));
        }
        g.out = out;
    }
    return g;
}

// --- Velocidad -------------------------------------------------------------------------------

std::string atempo_chain(double speed) {
    std::string out;
    double s = speed;
    while (s > 2.0 + 1e-9) {
        out += "atempo=2.0,";
        s /= 2.0;
    }
    while (s < 0.5 - 1e-9) {
        out += "atempo=0.5,";
        s /= 0.5;
    }
    return out + std::format("atempo={:.5f}", s);
}

std::string audio_speed_filters(const TimelineClip& clip) {
    std::vector<std::string> bits;
    if (clip.reverse) {
        bits.emplace_back("areverse");
    }
    const double sp = clip_speed(clip);
    if (std::abs(sp - 1.0) > 1e-3) {
        bits.push_back(clip.keep_pitch ? atempo_chain(sp) : std::format("asetrate=48000*{:.6f}", sp));
    }
    std::string out;
    for (const auto& b : bits) {
        out += (out.empty() ? "" : ",") + b;
    }
    return out;
}

// --- El clip en la mezcla --------------------------------------------------------------------

bool clip_mixes_audio(const TimelineClip& clip, const TimelineTrack& track) { return !track.muted && !clip.muted; }

std::vector<std::string> export_audio_steps(const TimelineClip& clip, int input, int m) {
    constexpr std::string_view kFormat = "aformat=sample_fmts=fltp:sample_rates=48000:channel_layouts=stereo";
    const double start = clip.start > 0.0 ? clip.start : 0.0; // max(0.0, start)
    // int(round(start · 1000)) de Python: un entero sin límite, así que se queda en doble y se
    // escribe con todas sus cifras (sin pasar por un entero de 64 bits).
    const double start_ms = std::nearbyint(start * 1000);
    const std::string asp = audio_speed_filters(clip);
    std::vector<std::string> steps;
    steps.push_back(std::format("[{}:a]atrim={:.3f}:{:.3f},asetpts=PTS-STARTPTS,{}{}[ain{}]", input, clip.in_point,
                                clip.out_point, kFormat, asp.empty() ? "" : "," + asp, m));
    AudioFxGraph fx = audio_fx_graph(clip, std::format("ain{}", m), std::format("ax{}", m));
    steps.insert(steps.end(), std::make_move_iterator(fx.steps.begin()), std::make_move_iterator(fx.steps.end()));
    std::string chain = std::format("[{}]{},{}", fx.out, kFormat, volume_filter(clip));
    if (start_ms > 0) {
        chain += std::format(",adelay={:.0f}:all=1", start_ms);
    }
    steps.push_back(chain + std::format("[a{}]", m));
    return steps;
}

Result<ExportInputs> export_inputs(const Timeline& timeline, const ClipPredicate& media_exists,
                                   const ClipPredicate& has_audio) {
    std::map<std::string, const TimelineTrack*> track_by_id; // como el dict de Python: manda la última
    for (const TimelineTrack& t : timeline.tracks) {
        track_by_id[t.id] = &t;
    }
    std::vector<const TimelineClip*> vclips;
    std::vector<const TimelineClip*> aclips;
    for (const auto& cow : timeline.clips) {
        const TimelineClip& c = *cow;
        const auto it = track_by_id.find(c.track_id);
        if (it == track_by_id.end() || c.disabled || c.kind == "adjustment") {
            continue;
        }
        const TimelineTrack& track = *it->second;
        if (clip_timeline_duration(c) <= 0.02 || !media_exists(c)) {
            continue;
        }
        bool fits = false;
        if (track.kind == "video") {
            auto f = clip_fits_track(c.kind, "video");
            if (!f) {
                return std::unexpected(f.error());
            }
            fits = *f;
        }
        if (fits) {
            if (!track.hidden) {
                vclips.push_back(&c);
            }
            if (!is_still_clip(c) && clip_mixes_audio(c, track) && has_audio(c)) {
                aclips.push_back(&c);
            }
        } else if (track.kind == "audio") {
            if (clip_mixes_audio(c, track) && has_audio(c)) {
                aclips.push_back(&c);
            }
        }
    }
    ExportInputs out;
    std::map<std::string, int> idx_of;
    for (const auto* list : {&vclips, &aclips}) {
        for (const TimelineClip* c : *list) {
            if (idx_of.try_emplace(c->id, static_cast<int>(out.files.size())).second) {
                out.files.push_back(c);
            }
        }
    }
    for (const TimelineClip* c : aclips) {
        out.audio.emplace_back(c, idx_of.at(c->id));
    }
    return out;
}

Result<std::vector<std::string>> export_audio_graph(const Timeline& timeline, const ClipPredicate& media_exists,
                                                    const ClipPredicate& has_audio) {
    auto inputs = export_inputs(timeline, media_exists, has_audio);
    if (!inputs) {
        return std::unexpected(inputs.error());
    }
    std::vector<std::string> steps;
    int m = 0;
    for (const auto& [clip, input] : inputs->audio) {
        auto s = export_audio_steps(*clip, input, m++);
        steps.insert(steps.end(), std::make_move_iterator(s.begin()), std::make_move_iterator(s.end()));
    }
    if (m > 0) {
        steps.push_back(export_audio_mix(m, export_total_duration(timeline), timeline.audio_target_db));
    }
    return steps;
}

std::string export_audio_mix(int count, double total, double target_db) {
    if (count <= 0) {
        return {};
    }
    const double capped = target_db < -5.0 ? target_db : -5.0;  // min(-5.0, target)
    const double target = capped > -40.0 ? capped : -40.0;      // max(-40.0, …)
    const std::string loudnorm = std::format("loudnorm=I={:.1f}:TP=-1.5:LRA=11", target);
    if (count == 1) {
        return std::format("[a0]apad=whole_dur={:.3f},{}[aout]", total, loudnorm);
    }
    std::string joined;
    for (int i = 0; i < count; ++i) {
        joined += std::format("[a{}]", i);
    }
    return std::format("{}amix=inputs={}:normalize=0:dropout_transition=0,apad=whole_dur={:.3f},{}[aout]", joined, count,
                       total, loudnorm);
}

} // namespace gc::core
