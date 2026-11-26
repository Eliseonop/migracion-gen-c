#include "gc/core/live_capture.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>

#include "gc/core/io.hpp"
#include "gc/core/segments.hpp"
#include "gc/core/yt_ui.hpp"

namespace gc::core {

namespace {

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && is_space(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && is_space(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

std::string lower_ascii(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

bool starts_with_nocase(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && lower_ascii(s.substr(0, prefix.size())) == prefix;
}

// Un usuario de TikTok (el [\w.-]+ de yt-dlp, en ASCII).
bool tiktok_user(std::string_view s) {
    return !s.empty() && std::ranges::all_of(s, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
               c == '-';
    });
}

bool digits(std::string_view s) {
    return !s.empty() && std::ranges::all_of(s, [](char c) { return c >= '0' && c <= '9'; });
}

// Los trozos de la ruta, sin los vacíos.
std::vector<std::string_view> path_parts(std::string_view path) {
    std::vector<std::string_view> out;
    while (!path.empty()) {
        const std::size_t slash = path.find('/');
        const std::string_view part = path.substr(0, slash);
        if (!part.empty()) {
            out.push_back(part);
        }
        if (slash == std::string_view::npos) {
            break;
        }
        path.remove_prefix(slash + 1);
    }
    return out;
}

std::optional<LiveLink> tiktok_link(std::string_view host, std::string_view path) {
    const std::vector<std::string_view> parts = path_parts(path);
    if (host == "tiktok.com" && !parts.empty() && parts[0].size() > 1 && parts[0].front() == '@') {
        const std::string_view user = parts[0].substr(1);
        if (tiktok_user(user) && (parts.size() == 1 || (parts.size() == 2 && parts[1] == "live"))) {
            return LiveLink{LivePlatform::TikTok, "https://www.tiktok.com/@" + std::string(user) + "/live"};
        }
        return std::nullopt; // un vídeo (o lo que sea) de ese usuario
    }
    if (host == "m.tiktok.com" && parts.size() == 3 && parts[0] == "share" && parts[1] == "live" && digits(parts[2])) {
        return LiveLink{LivePlatform::TikTok, "https://m.tiktok.com/share/live/" + std::string(parts[2])};
    }
    return std::nullopt;
}

double round_ms(double s) {
    return std::round(s * 1000.0) / 1000.0;
}

std::optional<double> number(std::string_view s) {
    double v = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || end != s.data() + s.size() || !std::isfinite(v)) {
        return std::nullopt;
    }
    return v;
}

std::string text_of(const Json& info, const char* key) {
    const auto it = info.find(key);
    return it != info.end() && it->is_string() ? it->get<std::string>() : std::string();
}

} // namespace

std::optional<LiveLink> live_link(std::string_view text) {
    const std::string_view s = trim(text);
    if (s.empty()) {
        return std::nullopt;
    }
    std::string_view rest = s;
    if (starts_with_nocase(rest, "https://")) {
        rest.remove_prefix(8);
    } else if (starts_with_nocase(rest, "http://")) {
        rest.remove_prefix(7);
    } else if (rest.find("://") != std::string_view::npos) {
        return std::nullopt; // otro esquema
    }
    const std::size_t host_end = rest.find_first_of("/?#");
    std::string host = lower_ascii(rest.substr(0, host_end));
    if (const std::size_t at = host.rfind('@'); at != std::string::npos) {
        host.erase(0, at + 1); // usuario:clave@
    }
    if (const std::size_t colon = host.find(':'); colon != std::string::npos) {
        host.erase(colon); // el puerto
    }
    if (host.starts_with("www.")) {
        host.erase(0, 4);
    }
    std::string_view path = host_end == std::string_view::npos ? std::string_view() : rest.substr(host_end);
    path = path.substr(0, path.find_first_of("?#"));
    if (host == "tiktok.com" || host == "m.tiktok.com") {
        return tiktok_link(host, path);
    }
    if (yt_is_url(s)) {
        const bool scheme = starts_with_nocase(s, "https://") || starts_with_nocase(s, "http://");
        return LiveLink{LivePlatform::YouTube, scheme ? std::string(s) : "https://" + std::string(s)};
    }
    return std::nullopt;
}

std::string_view live_platform_name(LivePlatform platform) {
    return platform == LivePlatform::TikTok ? "TikTok" : "YouTube";
}

std::vector<std::string> live_resolve_args(const LiveLink& link) {
    // «best»: el mejor formato con vídeo y audio juntos (los de un directo de TikTok y los HLS de YouTube lo son).
    return {"-J", "--no-warnings", "--no-playlist", "-f", "best", link.url};
}

Result<LiveStream> live_stream_from_info(const Json& info) {
    if (!info.is_object()) {
        return fail(ErrorCode::Parse, "yt-dlp no devolvió nada que se pueda leer.");
    }
    const std::string status = text_of(info, "live_status");
    const auto is_live = info.find("is_live");
    const bool live = status == "is_live" || (status.empty() && is_live != info.end() && is_live->is_boolean() &&
                                              is_live->get<bool>());
    if (!live) {
        if (status == "is_upcoming") {
            return fail(ErrorCode::InvalidArgument, "Ese directo aún no ha empezado.");
        }
        if (status == "was_live" || status == "post_live") {
            return fail(ErrorCode::InvalidArgument,
                        "Ese directo ya acabó. Si quedó publicado, bájalo entero en «Cargar clips».");
        }
        return fail(ErrorCode::InvalidArgument, "Ese enlace no es un directo en marcha.");
    }
    LiveStream out;
    out.url = text_of(info, "url");
    if (out.url.empty()) {
        return fail(ErrorCode::InvalidArgument, "yt-dlp no dio la dirección del directo.");
    }
    if (const auto headers = info.find("http_headers"); headers != info.end() && headers->is_object()) {
        for (const auto& [name, value] : headers->items()) {
            if (value.is_string()) {
                out.headers += name + ": " + value.get<std::string>() + "\r\n";
            }
        }
    }
    out.id = text_of(info, "id");
    out.title = text_of(info, "title");
    for (const char* key : {"uploader", "channel", "creator", "uploader_id"}) {
        out.uploader = text_of(info, key);
        if (!out.uploader.empty()) {
            break;
        }
    }
    return out;
}

std::string live_resolve_error(std::string_view stderr_text) {
    const std::string low = lower_ascii(stderr_text);
    if (low.find("not currently live") != std::string::npos || low.find("is offline") != std::string::npos) {
        return "No está en directo ahora mismo (o el directo ya acabó).";
    }
    if (low.find("will begin") != std::string::npos || low.find("premieres in") != std::string::npos) {
        return "Ese directo aún no ha empezado.";
    }
    if (low.find("private video") != std::string::npos || low.find("sign in") != std::string::npos ||
        low.find("login required") != std::string::npos) {
        return "Ese directo es privado o pide iniciar sesión.";
    }
    if (low.find("unsupported url") != std::string::npos) {
        return "yt-dlp no reconoce ese enlace.";
    }
    // La última línea «ERROR: [extractor] id: mensaje», sin lo de delante.
    std::string_view last;
    std::string_view text = stderr_text;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        const std::string_view line = trim(text.substr(0, nl));
        if (line.starts_with("ERROR:")) {
            last = trim(line.substr(6));
        }
        if (nl == std::string_view::npos) {
            break;
        }
        text.remove_prefix(nl + 1);
    }
    if (last.starts_with('[')) {
        if (const std::size_t close = last.find("] "); close != std::string_view::npos) {
            last = last.substr(close + 2);
            if (const std::size_t colon = last.find(": "); colon != std::string_view::npos) {
                last = last.substr(colon + 2);
            }
        }
    }
    return last.empty() ? std::string("yt-dlp no pudo abrir el directo.") : "yt-dlp: " + std::string(last);
}

std::vector<std::string> live_record_args(const LiveStream& stream, const std::filesystem::path& out) {
    std::vector<std::string> args{"-hide_banner", "-loglevel", "error", "-nostats", "-progress", "pipe:1",
                                  "-stats_period", "0.5",
                                  // Si el flujo se corta: reintenta unos segundos y, si no vuelve, acaba (15 s sin datos).
                                  "-reconnect", "1", "-reconnect_streamed", "1", "-reconnect_delay_max", "5",
                                  "-rw_timeout", "15000000"};
    if (!stream.headers.empty()) {
        args.insert(args.end(), {"-headers", stream.headers});
    }
    args.insert(args.end(), {"-i", stream.url, "-map", "0:v:0", "-map", "0:a:0?", "-c", "copy", "-f", "mpegts", "-y",
                             path_to_utf8(out)});
    return args;
}

std::vector<std::string> live_remux_args(const std::filesystem::path& ts, const std::filesystem::path& mp4) {
    return {"-hide_banner", "-loglevel", "error", "-nostdin", "-i", path_to_utf8(ts), "-map", "0:v:0", "-map",
            "0:a:0?", "-c", "copy", "-movflags", "+faststart", "-y", path_to_utf8(mp4)};
}

void live_apply_progress(LiveProgress& progress, std::string_view line) {
    line = trim(line);
    const std::size_t eq = line.find('=');
    if (eq == std::string_view::npos) {
        return;
    }
    const std::string_view key = line.substr(0, eq);
    const std::string_view value = trim(line.substr(eq + 1));
    if (key == "out_time_us") {
        if (const auto us = number(value); us && *us >= 0) {
            progress.seconds = *us / 1e6;
        }
    } else if (key == "total_size") {
        if (const auto bytes = number(value); bytes && *bytes >= 0) {
            progress.bytes = static_cast<std::int64_t>(*bytes);
        }
    } else if (key == "progress") {
        progress.ended = value == "end";
    }
}

std::vector<std::pair<double, double>> live_mark_ranges(std::span<const double> marks, double duration, double before,
                                                        double after) {
    std::vector<double> sorted;
    for (const double t : marks) {
        if (std::isfinite(t)) {
            sorted.push_back(t);
        }
    }
    std::ranges::sort(sorted);
    std::vector<std::pair<double, double>> out;
    for (const double t : sorted) {
        double start = std::max(0.0, t - std::max(0.0, before));
        double end = t + std::max(0.0, after);
        if (duration > 0) {
            end = std::min(end, duration);
        }
        start = round_ms(start);
        end = round_ms(end);
        if (!out.empty() && start <= out.back().second) {
            out.back().second = std::max(out.back().second, end);
        } else {
            out.emplace_back(start, end);
        }
    }
    std::erase_if(out, [](const auto& r) { return r.second - r.first < kMinSegment; });
    return out;
}

std::string live_file_stem(std::string_view title, LivePlatform platform, std::string_view stamp) {
    std::string clean;
    for (const char c : trim(title)) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || std::string_view(R"(<>:"/\|?*)").find(c) != std::string_view::npos) {
            clean += ' ';
        } else {
            clean += c;
        }
    }
    // Sin espacios repetidos ni puntos o espacios al final (Windows no los quiere).
    std::string squeezed;
    for (const char c : clean) {
        if (c != ' ' || (!squeezed.empty() && squeezed.back() != ' ')) {
            squeezed += c;
        }
    }
    if (squeezed.size() > 60) {
        std::size_t cut = 60;
        while (cut > 0 && (static_cast<unsigned char>(squeezed[cut]) & 0xC0U) == 0x80U) {
            --cut; // sin partir un carácter UTF-8
        }
        squeezed.erase(cut);
    }
    while (!squeezed.empty() && (squeezed.back() == ' ' || squeezed.back() == '.')) {
        squeezed.pop_back();
    }
    const std::string base = squeezed.empty() ? std::string(live_platform_name(platform)) : squeezed;
    return stamp.empty() ? base : base + " " + std::string(stamp);
}

} // namespace gc::core
