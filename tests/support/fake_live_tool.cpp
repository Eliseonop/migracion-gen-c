// Un yt-dlp y un FFmpeg de mentira para los tests de «Directo» (LiveRecorder): el primer argumento dice cuál es.
//
//   fake_live_tool yt-dlp … <url>
//       El JSON de `yt-dlp -J` de un directo de TikTok; si la URL lleva «offline», el error de yt-dlp (código 1).
//   fake_live_tool ffmpeg --video <mp4> … -progress pipe:1 … <salida>
//       La grabación: escribe en la salida y da el progreso cada 20 ms (0,1 s del directo cada vez, hasta 1,9 s,
//       lo que dura el vídeo de --video) hasta leer «q» por stdin.
//   fake_live_tool ffmpeg --video <mp4> … <salida.mp4>
//       El paso a MP4: copia ahí el vídeo de --video.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace {

constexpr std::string_view kLiveJson =
    R"({"id": "7210809319192726273", "title": "Directo de prueba", "uploader": "prueba", "is_live": true, )"
    R"("live_status": "is_live", "url": "https://pull.example/stream.flv", "ext": "flv", )"
    R"("http_headers": {"User-Agent": "Mozilla/5.0", "Referer": "https://www.tiktok.com/"}})";

int fake_ytdlp(const std::vector<std::string>& args) {
    if (args.back().find("offline") != std::string::npos) {
        std::fputs("ERROR: [tiktok:live] offline: The channel is not currently live\n", stderr);
        return 1;
    }
    std::fwrite(kLiveJson.data(), 1, kLiveJson.size(), stdout);
    std::fputs("\n", stdout);
    return 0;
}

int fake_record(const std::filesystem::path& out) {
    std::atomic<bool> quit{false};
    std::thread reader([&quit] {
        for (int c = std::getchar(); c != EOF; c = std::getchar()) {
            if (c == 'q') {
                break;
            }
        }
        quit = true;
    });
    std::ofstream file(out, std::ios::binary | std::ios::trunc);
    const std::string chunk(1024, 'x');
    long long bytes = 0;
    double seconds = 0;
    while (!quit) {
        file.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        file.flush();
        bytes += static_cast<long long>(chunk.size());
        seconds = std::min(seconds + 0.1, 1.9);
        std::printf("out_time_us=%lld\ntotal_size=%lld\nprogress=continue\n", std::llround(seconds * 1e6), bytes);
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::printf("progress=end\n");
    std::fflush(stdout);
    reader.join();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() < 2) {
        return 2;
    }
    if (args.front() == "yt-dlp") {
        return fake_ytdlp(args);
    }
    if (args.front() != "ffmpeg") {
        return 2;
    }
    std::filesystem::path video;
    if (const auto it = std::ranges::find(args, "--video"); it != args.end() && it + 1 != args.end()) {
        video = *(it + 1);
    }
    const std::filesystem::path out = args.back();
    if (std::ranges::find(args, "-progress") != args.end()) {
        return fake_record(out);
    }
    std::error_code ec;
    std::filesystem::copy_file(video, out, std::filesystem::copy_options::overwrite_existing, ec);
    return ec ? 3 : 0;
}
