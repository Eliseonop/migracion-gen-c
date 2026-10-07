#include "gc/core/io.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <system_error>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gc::core {

namespace fs = std::filesystem;

std::filesystem::path path_from_utf8(std::string_view utf8) {
    std::u8string u8(utf8.size(), u8'\0');
    std::ranges::transform(utf8, u8.begin(), [](char c) { return static_cast<char8_t>(c); });
    return fs::path(u8);
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string u8 = path.u8string();
    std::string out(u8.size(), '\0');
    std::ranges::transform(u8, out.begin(), [](char8_t c) { return static_cast<char>(c); });
    return out;
}

Result<Json> read_json_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return fail(ErrorCode::NotFound, "no se puede abrir el archivo", path_to_utf8(path));
    }
    // Leer de una vez con el tamaño conocido (más rápido que istreambuf_iterator).
    const std::streamoff size = in.tellg();
    std::string text(size > 0 ? static_cast<std::size_t>(size) : 0U, '\0');
    in.seekg(0);
    if (size < 0 || !in.read(text.data(), size)) {
        return fail(ErrorCode::Io, "error al leer el archivo", path_to_utf8(path));
    }
    // BOM de UTF-8 (lo dejan algunos editores de Windows).
    if (text.starts_with("\xEF\xBB\xBF")) {
        text.erase(0, 3);
    }
    try {
        return Json::parse(text);
    } catch (const Json::parse_error& e) {
        return fail(ErrorCode::Parse, e.what(), path_to_utf8(path));
    }
}

namespace {

#ifdef _WIN32
std::wstring wide(const fs::path& p) { return p.wstring(); }

// os.replace de Windows falla con WinError 5/32 si otro proceso tiene el destino
// abierto (antivirus, indexador, IDE): se reintenta con espera creciente y, como
// último recurso, se sobrescribe en el sitio (igual que projects._replace_file).
bool replace_file(const fs::path& src, const fs::path& dest, std::string& error) {
    const std::wstring wsrc = wide(src);
    const std::wstring wdest = wide(dest);
    auto delay = std::chrono::milliseconds(40);
    DWORD last = 0;
    for (int attempt = 0; attempt < 12; ++attempt) {
        const DWORD attrs = GetFileAttributesW(wdest.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY) != 0) {
            SetFileAttributesW(wdest.c_str(), attrs & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
        }
        if (MoveFileExW(wsrc.c_str(), wdest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
            return true;
        }
        last = GetLastError();
        if (last != ERROR_ACCESS_DENIED && last != ERROR_SHARING_VIOLATION) {
            break;
        }
        std::this_thread::sleep_for(delay);
        delay = std::min(std::chrono::milliseconds(250), delay * 3 / 2);
    }
    // Sobrescribir en el sitio.
    std::error_code ec;
    {
        std::ifstream in(src, std::ios::binary);
        std::ofstream out(dest, std::ios::binary | std::ios::trunc);
        if (in && out && (out << in.rdbuf()) && out.flush()) {
            in.close();
            fs::remove(src, ec);
            return true;
        }
    }
    error = "no se pudo reemplazar el archivo (error de Windows " + std::to_string(last) + ")";
    return false;
}
#else
bool replace_file(const fs::path& src, const fs::path& dest, std::string& error) {
    std::error_code ec;
    fs::rename(src, dest, ec); // rename() de POSIX ya es atómico y reemplaza
    if (ec) {
        error = ec.message();
        return false;
    }
    return true;
}
#endif

} // namespace

Result<void> write_json_file(const std::filesystem::path& path, const Json& value, int indent) {
    std::string text;
    try {
        text = value.dump(indent, ' ', false, Json::error_handler_t::strict);
    } catch (const Json::type_error& e) {
        return fail(ErrorCode::InvalidArgument, e.what(), path_to_utf8(path));
    }
    text.push_back('\n');

    std::error_code ec;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
    }
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out || !out.write(text.data(), static_cast<std::streamsize>(text.size())) || !out.flush()) {
            return fail(ErrorCode::Io, "no se pudo escribir el temporal", path_to_utf8(tmp));
        }
    }
    std::string error;
    if (!replace_file(tmp, path, error)) {
        fs::remove(tmp, ec);
        return fail(ErrorCode::Io, error, path_to_utf8(path));
    }
    return {};
}

} // namespace gc::core
