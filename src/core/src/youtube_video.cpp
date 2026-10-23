#include "gc/core/youtube_video.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <functional>
#include <iterator>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "edit_util.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

using detail::field;
using detail::invalid;
using detail::py_float;
using detail::py_isdecimal;
using detail::py_lower;
using detail::py_str;
using detail::py_truthy;
using detail::py_upper;
using detail::utf8_next;

namespace {

constexpr std::array<std::pair<std::string_view, int>, 5> kCodecRank{
    {{"avc1", 0}, {"h264", 0}, {"vp09", 1}, {"vp9", 1}, {"av01", 2}}};

// _num: float(v or 0), o 0 si no se puede.
double pnum(const Json& v) {
    if (!py_truthy(v)) {
        return 0.0;
    }
    return py_float(v).value_or(0.0);
}

// int(x) de un float: NaN e infinitos fallan (ValueError, OverflowError).
Result<std::int64_t> int_of(double x) {
    const double t = std::trunc(x);
    if (!std::isfinite(t) || t < -9.2233720368547758e18 || t >= 9.2233720368547758e18) {
        return invalid("no se puede pasar a entero un NaN o un infinito");
    }
    return static_cast<std::int64_t>(t);
}

// v or default
const Json& or_default(const Json& v, const Json& def) { return py_truthy(v) ? v : def; }

const Json kEmpty = Json("");
const Json kNone = Json("none");
const Json kSdr = Json("SDR");

// El valor de un dígito decimal de Unicode: los de cada sistema van en tramos de 10 seguidos.
int digit_value(char32_t c) {
    char32_t start = c;
    while (start > 0 && py_isdecimal(start - 1)) {
        --start;
    }
    return static_cast<int>((c - start) % 10);
}

// _NOTE_RE.search(note) con r"(\d{3,4})p": el número del primer «NNNp» o «NNNNp».
std::optional<std::int64_t> note_quality(const std::string& note) {
    std::vector<char32_t> cps;
    for (std::size_t i = 0; i < note.size();) {
        cps.push_back(utf8_next(note, i));
    }
    for (std::size_t i = 0; i < cps.size(); ++i) {
        std::size_t run = 0;
        while (i + run < cps.size() && run < 4 && py_isdecimal(cps[i + run])) {
            ++run;
        }
        for (std::size_t len = run; len >= 3; --len) { // {3,4} codicioso: primero 4
            if (i + len < cps.size() && cps[i + len] == U'p') {
                std::int64_t v = 0;
                for (std::size_t k = 0; k < len; ++k) {
                    v = (v * 10) + digit_value(cps[i + k]);
                }
                return v;
            }
        }
    }
    return std::nullopt;
}

Result<bool> is_video(const Json& f) {
    if (or_default(field(f, "vcodec"), kEmpty) == kNone) {
        return false;
    }
    if (py_truthy(field(f, "has_drm")) || or_default(field(f, "ext"), kEmpty) == Json("mhtml")) { // storyboards
        return false;
    }
    if (!py_truthy(field(f, "format_id"))) {
        return false;
    }
    auto q = youtube_format_quality(f);
    if (!q) {
        return std::unexpected(q.error());
    }
    return q->has_value();
}

bool is_audio(const Json& f) {
    return or_default(field(f, "vcodec"), kEmpty) == kNone && or_default(field(f, "acodec"), kNone) != kNone;
}

// `(x or "").lower()` y compañía: un valor que no es texto falla (AttributeError).
Result<std::string> text_attr(const Json& v, const Json& def) {
    const Json& t = or_default(v, def);
    if (!t.is_string()) {
        return invalid("se esperaba texto");
    }
    return t.get<std::string>();
}

Result<int> codec_rank(const Json& f) {
    auto vc = text_attr(field(f, "vcodec"), kEmpty);
    if (!vc) {
        return std::unexpected(vc.error());
    }
    const std::string low = py_lower(*vc);
    for (const auto& [prefix, rank] : kCodecRank) {
        if (low.starts_with(prefix)) {
            return rank;
        }
    }
    return 3;
}

// _rank: (sdr, fps, directo, −codec, tbr, filesize), comparado como una tupla de Python.
struct Rank {
    bool sdr = false;
    double fps = 0.0;
    bool direct = false;
    int codec = 0;
    double tbr = 0.0;
    double filesize = 0.0;
};

bool greater(const Rank& a, const Rank& b) {
    if (a.sdr != b.sdr) {
        return a.sdr;
    }
    if (a.fps != b.fps) {
        return a.fps > b.fps;
    }
    if (a.direct != b.direct) {
        return a.direct;
    }
    if (a.codec != b.codec) {
        return a.codec > b.codec;
    }
    if (a.tbr != b.tbr) {
        return a.tbr > b.tbr;
    }
    return a.filesize > b.filesize;
}

Result<Rank> rank_of(const Json& f) {
    auto dr = text_attr(field(f, "dynamic_range"), kSdr);
    if (!dr) {
        return std::unexpected(dr.error());
    }
    // "m3u8" not in (protocol or ""): en un texto, como subcadena; en una lista, como elemento.
    const Json& proto = or_default(field(f, "protocol"), kEmpty);
    bool hls = false;
    if (proto.is_string()) {
        hls = proto.get_ref<const std::string&>().contains("m3u8");
    } else if (proto.is_array()) {
        for (const Json& x : proto) {
            hls = hls || x == Json("m3u8");
        }
    } else if (proto.is_object()) {
        hls = proto.contains("m3u8");
    } else {
        return invalid("protocol no es iterable");
    }
    auto codec = codec_rank(f);
    if (!codec) {
        return std::unexpected(codec.error());
    }
    return Rank{py_upper(*dr) == "SDR", pnum(field(f, "fps")), !hls, -*codec, pnum(field(f, "tbr")),
                pnum(field(f, "filesize"))};
}

Result<const Json*> formats_of(const Json& info) {
    static const Json kNoFormats = Json::array();
    const Json& formats = or_default(field(info, "formats"), kNoFormats);
    if (!formats.is_array()) {
        return invalid("formats no es una lista");
    }
    for (const Json& f : formats) {
        if (!f.is_object()) {
            return invalid("un formato no es un objeto"); // f.get: AttributeError
        }
    }
    return &formats;
}

// _by_quality: el mejor formato de vídeo de cada calidad, en el orden en que aparece cada una.
Result<std::vector<std::pair<std::int64_t, const Json*>>> by_quality(const Json& formats) {
    std::vector<std::pair<std::int64_t, std::vector<const Json*>>> groups;
    for (const Json& f : formats) {
        auto video = is_video(f);
        if (!video) {
            return std::unexpected(video.error());
        }
        if (!*video) {
            continue;
        }
        const std::int64_t q = **youtube_format_quality(f);
        auto it = std::ranges::find(groups, q, &std::pair<std::int64_t, std::vector<const Json*>>::first);
        if (it == groups.end()) {
            groups.emplace_back(q, std::vector<const Json*>{});
            it = std::prev(groups.end());
        }
        it->second.push_back(&f);
    }
    std::vector<std::pair<std::int64_t, const Json*>> out;
    for (const auto& [q, fs] : groups) {
        const Json* best = nullptr;
        Rank best_rank;
        for (const Json* f : fs) { // max(): se queda el primero de los iguales
            auto r = rank_of(*f);
            if (!r) {
                return std::unexpected(r.error());
            }
            if (best == nullptr || greater(*r, best_rank)) {
                best = f;
                best_rank = *r;
            }
        }
        out.emplace_back(q, best);
    }
    return out;
}

const Json* best_audio(const Json& formats) {
    const Json* best = nullptr;
    std::tuple<bool, bool, double> best_key;
    for (const Json& f : formats) {
        if (!is_audio(f)) {
            continue;
        }
        const double abr = pnum(field(f, "abr"));
        const std::tuple<bool, bool, double> key{!py_str(field(f, "format_id")).ends_with("-drc"),
                                                 or_default(field(f, "ext"), kEmpty) == Json("m4a"),
                                                 abr != 0.0 ? abr : pnum(field(f, "tbr"))};
        if (best == nullptr || key > best_key) {
            best = &f;
            best_key = key;
        }
    }
    return best;
}

// _size: int(filesize or filesize_approx), 0 sin formato.
Result<std::int64_t> size_of(const Json* f) {
    if (f == nullptr) {
        return 0;
    }
    const double size = pnum(field(*f, "filesize"));
    return int_of(size != 0.0 ? size : pnum(field(*f, "filesize_approx")));
}

} // namespace

Result<std::optional<std::int64_t>> youtube_format_quality(const Json& f) {
    const Json& note = field(f, "format_note");
    if (auto q = note_quality(py_truthy(note) ? py_str(note) : std::string())) {
        return q;
    }
    auto w = int_of(pnum(field(f, "width")));
    if (!w) {
        return std::unexpected(w.error());
    }
    auto h = int_of(pnum(field(f, "height")));
    if (!h) {
        return std::unexpected(h.error());
    }
    if (*w != 0 && *h != 0) {
        return std::optional<std::int64_t>(std::min(*w, *h));
    }
    return *h != 0 ? std::optional<std::int64_t>(*h) : std::nullopt;
}

Result<Json> youtube_available_qualities(const Json& info) {
    auto formats = formats_of(info);
    if (!formats) {
        return std::unexpected(formats.error());
    }
    auto best = by_quality(**formats);
    if (!best) {
        return std::unexpected(best.error());
    }
    const Json* audio = best_audio(**formats);
    std::ranges::sort(*best, std::greater<>{}, &std::pair<std::int64_t, const Json*>::first);
    Json out = Json::array();
    Json good = Json::array();
    for (const auto& [q, f] : *best) {
        const bool needs_audio = or_default(field(*f, "acodec"), kEmpty) == kNone;
        auto video_size = size_of(f);
        if (!video_size) {
            return std::unexpected(video_size.error());
        }
        std::int64_t size = *video_size;
        if (size != 0) {
            auto audio_size = needs_audio ? size_of(audio) : Result<std::int64_t>(0);
            if (!audio_size) {
                return std::unexpected(audio_size.error());
            }
            size += *audio_size;
        }
        auto fps = int_of(std::nearbyint(pnum(field(*f, "fps")))); // int(round(x)): al par
        if (!fps) {
            return std::unexpected(fps.error());
        }
        Json item{{"quality", q}, {"fps", *fps != 0 ? Json(*fps) : Json()}, {"size", size != 0 ? Json(size) : Json()}};
        if (q >= kYoutubeMinQuality) {
            good.push_back(item);
        }
        out.push_back(std::move(item));
    }
    return good.empty() ? out : good;
}

std::optional<std::int64_t> youtube_resolve_quality(const Json& qualities, std::optional<std::int64_t> wanted) {
    std::vector<std::int64_t> avail;
    for (const Json& x : qualities) {
        avail.push_back(x.at("quality").get<std::int64_t>());
    }
    if (avail.empty()) {
        return std::nullopt;
    }
    if (!wanted || *wanted == 0) {
        return avail.front();
    }
    for (const std::int64_t q : avail) {
        if (q <= *wanted) {
            return q;
        }
    }
    return avail.back();
}

Result<std::string> youtube_format_spec(const Json& info, std::int64_t quality) {
    auto formats = formats_of(info);
    if (!formats) {
        return std::unexpected(formats.error());
    }
    auto best = by_quality(**formats);
    if (!best) {
        return std::unexpected(best.error());
    }
    const auto it = std::ranges::find(*best, quality, &std::pair<std::int64_t, const Json*>::first);
    if (it == best->end()) {
        return invalid(std::format("El vídeo no tiene calidad {}p.", quality));
    }
    const Json& f = *it->second;
    std::string fid = py_str(f.at("format_id"));
    if (or_default(field(f, "acodec"), kEmpty) != kNone) {
        return fid;
    }
    return std::format("{0}+bestaudio[ext=m4a]/{0}+bestaudio/{0}", fid);
}

std::string mb_label(double bytes) {
    const double mb = bytes / 1048576.0;
    return mb < 10 ? std::format("{:.1f} MB", mb) : std::format("{:.0f} MB", mb);
}

} // namespace gc::core
