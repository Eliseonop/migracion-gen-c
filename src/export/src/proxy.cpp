#include "gc/export/proxy.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "gc/core/io.hpp"
#include "gc/core/json.hpp"
#include "gc/media/decode.hpp"
#include "gc/media/encode.hpp"
#include "gc/media/probe.hpp"

namespace gc::exporter {

namespace {

namespace fs = std::filesystem;

// Sube si cambia cómo se hace un proxy: los de antes dejan de valer.
constexpr int kProxyVersion = 1;

// Lo que identifica la versión del archivo original.
struct SourceId {
    std::uintmax_t size = 0;
    std::int64_t mtime = 0;
};

std::optional<SourceId> source_id(const fs::path& source) {
    std::error_code ec;
    const auto size = fs::file_size(source, ec);
    if (ec) {
        return std::nullopt;
    }
    const auto time = fs::last_write_time(source, ec);
    if (ec) {
        return std::nullopt;
    }
    return SourceId{size, static_cast<std::int64_t>(time.time_since_epoch().count())};
}

// FNV-1a de la ruta absoluta, el tamaño, la fecha, los fps y la versión: el nombre en la caché.
fs::path proxy_base(const fs::path& cache_dir, const fs::path& source, const SourceId& id, int fps) {
    std::uint64_t h = 0xCBF29CE484222325ULL;
    const auto mix = [&h](std::string_view s) {
        for (const char c : s) {
            h = (h ^ static_cast<unsigned char>(c)) * 0x100000001B3ULL;
        }
        h = (h ^ 0xFFU) * 0x100000001B3ULL; // separador
    };
    std::error_code ec;
    const fs::path absolute = fs::absolute(source, ec);
    mix(core::path_to_utf8((ec ? source : absolute).lexically_normal()));
    mix(std::to_string(id.size));
    mix(std::to_string(id.mtime));
    mix(std::to_string(fps));
    mix(std::to_string(kProxyVersion));
    return cache_dir / std::format("{:016x}", h);
}

fs::path with_suffix(fs::path base, std::string_view suffix) {
    base += std::string(suffix);
    return base;
}

bool has_alpha(const media::RgbaFrame& frame) {
    for (std::size_t i = 3; i < frame.pixels.size(); i += 4) {
        if (frame.pixels[i] != 255) {
            return true;
        }
    }
    return false;
}

bool is_webm(const fs::path& source) {
    std::string ext = core::path_to_utf8(source.extension());
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".webm";
}

} // namespace

std::optional<std::pair<int, int>> proxy_size(int width, int height) {
    if (width <= 0 || height <= 0 || std::min(width, height) <= kProxyFromShortSide) {
        return std::nullopt;
    }
    const double k = static_cast<double>(kProxyShortSide) / std::min(width, height);
    const auto even = [](double v) { return std::max(2, static_cast<int>(std::lround(v / 2.0)) * 2); };
    if (width <= height) {
        return std::pair{kProxyShortSide, even(height * k)};
    }
    return std::pair{even(width * k), kProxyShortSide};
}

ProxyLookup find_proxy(const fs::path& cache_dir, const fs::path& source, int fps) {
    fps = fps > 0 ? fps : 30;
    const auto id = source_id(source);
    if (!id) {
        return {};
    }
    const fs::path base = proxy_base(cache_dir, source, *id, fps);
    auto meta = core::read_json_file(with_suffix(base, ".json"));
    if (!meta || !meta->is_object() || meta->value("version", 0) != kProxyVersion ||
        meta->value("size", std::uintmax_t{0}) != id->size || meta->value("mtime", std::int64_t{0}) != id->mtime ||
        meta->value("fps", 0) != fps) {
        return {};
    }
    if (!meta->value("proxy", false)) {
        return {ProxyState::NotNeeded, {}};
    }
    std::error_code ec;
    const fs::path mp4 = with_suffix(base, ".mp4");
    if (!fs::is_regular_file(mp4, ec)) {
        return {};
    }
    return {ProxyState::Ready, {mp4, meta->value("source_width", 0), meta->value("source_height", 0)}};
}

Result<ProxyLookup> make_proxy(const fs::path& cache_dir, const fs::path& source, int fps,
                               const std::function<bool(double)>& progress) {
    fps = fps > 0 ? fps : 30;
    const auto id = source_id(source);
    if (!id) {
        return fail(ErrorCode::Io, std::format("no se puede leer {}", core::path_to_utf8(source)));
    }
    std::error_code ec;
    fs::create_directories(cache_dir, ec);
    if (ec) {
        return fail(ErrorCode::Io, std::format("no se puede crear la caché de proxies {}", core::path_to_utf8(cache_dir)));
    }
    const fs::path base = proxy_base(cache_dir, source, *id, fps);
    const fs::path json = with_suffix(base, ".json");
    Json meta = {{"version", kProxyVersion},
                 {"source", core::path_to_utf8(source)},
                 {"size", id->size},
                 {"mtime", id->mtime},
                 {"fps", fps}};
    const auto not_needed = [&] -> Result<ProxyLookup> {
        meta["proxy"] = false;
        if (auto ok = core::write_json_file(json, meta); !ok) {
            return std::unexpected(ok.error());
        }
        return ProxyLookup{ProxyState::NotNeeded, {}};
    };
    // Un WebM se lee con libvpx-vp9 por su alfa, que el H.264 no guarda.
    if (is_webm(source)) {
        return not_needed();
    }
    // El primer fotograma como lo decodifica el export (con el autogiro): su tamaño y si tiene alfa.
    auto first = media::frame_at(source, 0.0);
    if (!first) {
        return std::unexpected(first.error());
    }
    if (!first->has_value() || has_alpha(**first)) {
        return not_needed();
    }
    const int source_w = (*first)->width;
    const int source_h = (*first)->height;
    const auto size = proxy_size(source_w, source_h);
    if (!size) {
        return not_needed();
    }
    const auto [w, h] = *size;
    const media::MediaProbe probe = media::probe_media(source);

    // Los fotogramas del export (el `color_pre` si no declara matriz y `fps=F`), desde 0 y reducidos.
    media::DecodeSpec spec;
    spec.input_args = {"-i", core::path_to_utf8(source)};
    spec.chain = std::format("{}fps={}:start_time=0,scale={}:{}:flags=bicubic",
                             probe.color_untagged ? std::string(media::kAssumeBt709Filter) + "," : std::string(), fps,
                             w, h);
    media::EncodeSettings settings;
    settings.width = w;
    settings.height = h;
    settings.fps = fps;
    settings.crf = 23;
    settings.preset = "veryfast";
    settings.audio = false;
    const fs::path part = with_suffix(base, ".part.mp4");
    auto opened = media::Mp4Writer::open(part, settings);
    if (!opened) {
        return std::unexpected(opened.error());
    }
    std::optional<media::Mp4Writer> writer(std::move(*opened));
    const double expected = std::max(1.0, probe.duration * fps);
    std::size_t frames = 0;
    bool stopped = false;
    std::optional<Error> write_error;
    auto decoded = media::decode_frames(spec, [&](const media::RgbaFrame& frame) {
        if (frame.width != w || frame.height != h) {
            write_error = Error{ErrorCode::Internal,
                                std::format("el proxy salió de {}x{} y no de {}x{}", frame.width, frame.height, w, h), {}};
            return false;
        }
        if (auto ok = writer->video(frame.pixels); !ok) {
            write_error = ok.error();
            return false;
        }
        ++frames;
        if (progress && !progress(std::min(1.0, static_cast<double>(frames) / expected))) {
            stopped = true;
            return false;
        }
        return true;
    });
    const auto discard = [&] {
        writer.reset(); // cierra el archivo antes de borrarlo
        std::error_code ignored;
        fs::remove(part, ignored);
    };
    if (stopped) {
        discard();
        return ProxyLookup{};
    }
    if (write_error || !decoded) {
        discard();
        return std::unexpected(write_error ? *write_error : decoded.error());
    }
    if (frames == 0) {
        discard();
        return not_needed();
    }
    if (auto ok = writer->finish(); !ok) {
        discard();
        return std::unexpected(ok.error());
    }
    writer.reset();
    const fs::path mp4 = with_suffix(base, ".mp4");
    fs::rename(part, mp4, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(part, ignored);
        return fail(ErrorCode::Io, std::format("no se pudo dejar el proxy en {}", core::path_to_utf8(mp4)));
    }
    meta["proxy"] = true;
    meta["source_width"] = source_w;
    meta["source_height"] = source_h;
    meta["width"] = w;
    meta["height"] = h;
    meta["frames"] = frames;
    if (auto ok = core::write_json_file(json, meta); !ok) {
        return std::unexpected(ok.error());
    }
    return ProxyLookup{ProxyState::Ready, {mp4, source_w, source_h}};
}

} // namespace gc::exporter
