#include "pillow_font.hpp"

#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>

#include <ft2build.h>
#include FT_FREETYPE_H

namespace gc::render::detail {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kMaxStringLength = 1'000'000;      // MAX_STRING_LENGTH de ImageFont
constexpr std::size_t kMaxCached = std::size_t{1} << 16; // medidas por archivo antes de vaciar su caché

// Caracteres de un texto UTF-8 sin decodificarlo: len() de Python.
std::size_t char_count(std::string_view s) noexcept {
    std::size_t n = 0;
    for (const char c : s) {
        n += static_cast<std::size_t>((static_cast<unsigned char>(c) & 0xC0U) != 0x80U);
    }
    return n;
}

// Los caracteres de un texto UTF-8, los de un str de Python. Un byte que no es UTF-8 válido va como
// U+FFFD (de un JSON no llega ninguno).
std::vector<std::uint32_t> code_points(std::string_view s) {
    std::vector<std::uint32_t> out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t n = 0;
        std::uint32_t cp = 0;
        if (c < 0x80U) {
            n = 1;
            cp = c;
        } else if (c >= 0xC2U && c < 0xE0U) {
            n = 2;
            cp = c & 0x1FU;
        } else if (c >= 0xE0U && c < 0xF0U) {
            n = 3;
            cp = c & 0x0FU;
        } else if (c >= 0xF0U && c < 0xF5U) {
            n = 4;
            cp = c & 0x07U;
        }
        bool valid = n > 0 && i + n <= s.size();
        for (std::size_t k = 1; valid && k < n; ++k) {
            const auto cc = static_cast<unsigned char>(s[i + k]);
            valid = (cc & 0xC0U) == 0x80U;
            cp = (cp << 6U) | (cc & 0x3FU);
        }
        if (!valid) {
            out.push_back(0xFFFDU);
            ++i;
            continue;
        }
        out.push_back(cp);
        i += n;
    }
    return out;
}

// `width = size * 64` de getfont(): `size` en float de C, la multiplicación en float y el resultado
// en un FT_Long de 32 bits (Windows). Fuera de rango, MSVC da el entero indefinido (INT_MIN), que
// FT_Request_Size rechaza por negativo: Pillow lanza.
std::optional<FT_Long> request_width(double size) {
    constexpr double kFloatMax = std::numeric_limits<float>::max();
    if (!(size >= -kFloatMax && size <= kFloatMax)) { // un double que no cabe en float: inf en MSVC
        return std::nullopt;
    }
    const float scaled = static_cast<float>(size) * 64.0F;
    if (!(scaled >= 0.0F && scaled < 2147483648.0F)) {
        return std::nullopt;
    }
    return static_cast<FT_Long>(scaled);
}

// Una variable de entorno; vacía si no está.
std::string env(const char* name) {
    const char* v = std::getenv(name); // NOLINT(concurrency-mt-unsafe): nadie la cambia mientras corre
    return v != nullptr ? std::string(v) : std::string();
}

// Las carpetas donde ImageFont.truetype busca una fuente que no ha podido abrir, por plataforma.
std::vector<fs::path> fallback_dirs() {
    std::vector<fs::path> out;
#if defined(_WIN32)
    if (const auto windir = env("WINDIR"); !windir.empty()) {
        out.push_back(fs::path(windir) / "fonts");
    }
#elif defined(__APPLE__)
    out.emplace_back("/Library/Fonts");
    out.emplace_back("/System/Library/Fonts");
    out.push_back(fs::path(env("HOME")) / "Library/Fonts");
#elif defined(__linux__)
    auto data_home = env("XDG_DATA_HOME");
    if (data_home.empty()) {
        data_home = env("HOME") + "/.local/share";
    }
    std::vector<std::string> dirs{data_home};
    auto data_dirs = env("XDG_DATA_DIRS");
    if (data_dirs.empty()) {
        data_dirs = "/usr/local/share:/usr/share";
    }
    for (std::size_t pos = 0;;) {
        const auto colon = data_dirs.find(':', pos);
        dirs.push_back(data_dirs.substr(pos, colon == std::string::npos ? std::string::npos : colon - pos));
        if (colon == std::string::npos) {
            break;
        }
        pos = colon + 1;
    }
    for (const auto& d : dirs) {
        out.push_back(fs::path(d) / "fonts");
    }
#endif
    return out;
}

// La búsqueda de truetype en una carpeta, en el orden de os.walk (de arriba abajo: los archivos de
// cada carpeta antes que sus subcarpetas, sin seguir los enlaces a carpetas). Con extensión, el
// primero que se llama igual; sin ella, el primero con ese nombre y extensión .ttf y, si no hay,
// el primero con otra (`other`).
std::optional<fs::path> walk(const fs::path& dir, const fs::path& name, std::optional<fs::path>& other) {
    std::error_code ec;
    fs::directory_iterator it(dir, ec);
    if (ec) {
        return std::nullopt; // os.walk se salta lo que no puede leer
    }
    std::vector<fs::path> files;
    std::vector<fs::path> subdirs;
    for (const fs::directory_iterator end; it != end; it.increment(ec)) {
        if (ec) {
            break;
        }
        const fs::directory_entry& e = *it;
        std::error_code type_ec;
        if (e.is_directory(type_ec) && !type_ec) {
            if (!e.is_symlink(type_ec)) {
                subdirs.push_back(e.path());
            }
        } else {
            files.push_back(e.path());
        }
    }
    const bool has_ext = !name.extension().empty();
    for (const auto& f : files) {
        if (has_ext && f.filename() == name) {
            return f;
        }
        if (!has_ext && f.stem() == name) {
            if (f.extension() == ".ttf") {
                return f;
            }
            if (!other) {
                other = f;
            }
        }
    }
    for (const auto& d : subdirs) {
        if (auto found = walk(d, name, other)) {
            return found;
        }
    }
    return std::nullopt;
}

// Una fuente abierta: los bytes (FT_New_Memory_Face no los copia), la cara y la última petición de
// tamaño que salió bien.
struct Face {
    std::vector<unsigned char> bytes;
    FT_Face face = nullptr;
    FT_Long width = -1;
};

} // namespace

struct PillowFonts::Impl {
    std::mutex mutex;
    FT_Library library = nullptr;
    std::map<fs::path, std::unique_ptr<Face>> faces;                 // nullptr: no se abre
    std::map<fs::path, std::optional<fs::path>> fallbacks;           // nombre → lo que encuentra truetype
    std::map<fs::path, std::unordered_map<std::string, std::optional<std::int64_t>>> lengths; // «ancho\ntexto»

    Impl() {
        if (FT_Init_FreeType(&library) != 0) {
            library = nullptr;
        }
    }
    ~Impl() {
        for (auto& [path, f] : faces) {
            if (f && f->face != nullptr) {
                FT_Done_Face(f->face);
            }
        }
        if (library != nullptr) {
            FT_Done_FreeType(library);
        }
    }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    // FT_New_Face de getfont(): la cara del archivo, o nada si FreeType no la abre.
    Face* face(const fs::path& path) {
        if (const auto it = faces.find(path); it != faces.end()) {
            return it->second.get();
        }
        auto f = std::make_unique<Face>();
        std::ifstream in(path, std::ios::binary);
        bool ok = library != nullptr && static_cast<bool>(in);
        if (ok) {
            f->bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            // Un archivo vacío tampoco se abre (FreeType no reconoce el formato).
            ok = !f->bytes.empty() && f->bytes.size() <= static_cast<std::size_t>(std::numeric_limits<FT_Long>::max()) &&
                 FT_New_Memory_Face(library, f->bytes.data(), static_cast<FT_Long>(f->bytes.size()), 0, &f->face) == 0;
        }
        if (!ok) {
            f.reset();
        }
        return faces.emplace(path, std::move(f)).first->second.get();
    }

    // getfont() entera: la cara con el tamaño pedido (FT_Request_Size), o nada si falla algo (OSError).
    Face* sized(const fs::path& path, FT_Long width) {
        Face* f = face(path);
        if (f == nullptr) {
            return nullptr;
        }
        if (f->width != width) {
            FT_Size_RequestRec req{};
            req.type = FT_SIZE_REQUEST_TYPE_NOMINAL;
            req.width = width;
            req.height = width;
            req.horiResolution = 0;
            req.vertResolution = 0;
            if (FT_Request_Size(f->face, &req) != 0) {
                f->width = -1;
                return nullptr;
            }
            f->width = width;
        }
        return f;
    }

    // ImageFont.truetype(path, size): si getfont() lanza OSError, prueba con el primer archivo que se
    // llame igual en las carpetas de fuentes del sistema (WINDIR\fonts en Windows) y, si tampoco,
    // lanza.
    Face* truetype(const fs::path& path, FT_Long width) {
        if (Face* f = sized(path, width)) {
            return f;
        }
        const fs::path name = path.filename();
        auto it = fallbacks.find(name);
        if (it == fallbacks.end()) {
            std::optional<fs::path> found;
            std::optional<fs::path> other;
            for (const auto& dir : fallback_dirs()) {
                found = walk(dir, name, other);
                if (found) {
                    break;
                }
            }
            it = fallbacks.emplace(name, found ? found : other).first;
        }
        return it->second ? sized(*it->second, width) : nullptr;
    }

    // text_layout_fallback de _imagingft.c con la cara ya en su tamaño.
    static std::optional<PillowLayout> run(Face& f, std::string_view text) {
        PillowLayout out;
        out.ascender = f.face->size->metrics.ascender;
        out.descender = f.face->size->metrics.descender;
        const bool kerning = FT_HAS_KERNING(f.face);
        const auto cps = code_points(text);
        out.glyphs.reserve(cps.size());
        out.advances.reserve(cps.size());
        FT_UInt last = 0;
        for (const std::uint32_t cp : cps) {
            const FT_UInt index = FT_Get_Char_Index(f.face, cp);
            if (FT_Load_Glyph(f.face, index, FT_LOAD_DEFAULT) != 0) {
                return std::nullopt;
            }
            if (kerning && last != 0 && index != 0) {
                FT_Vector delta{};
                if (FT_Get_Kerning(f.face, last, index, FT_KERNING_DEFAULT, &delta) == 0) {
                    out.advances.back() = static_cast<std::int32_t>(out.advances.back() + ft_pixel(delta.x));
                }
            }
            out.glyphs.push_back(index);
            out.advances.push_back(static_cast<std::int32_t>(f.face->glyph->metrics.horiAdvance));
            last = index;
        }
        // Pillow suma en un `int` (32 bits, con la vuelta de MSVC si se pasa).
        std::uint32_t sum = 0;
        for (const std::int64_t a : out.advances) {
            sum += static_cast<std::uint32_t>(a);
        }
        out.length = static_cast<std::int32_t>(sum);
        return out;
    }
};

PillowFonts::PillowFonts() : impl_(std::make_unique<Impl>()) {}
PillowFonts::~PillowFonts() = default;

std::optional<PillowLayout> PillowFonts::layout(const std::filesystem::path& path, double size,
                                                std::string_view text) {
    const auto width = request_width(size);
    if (!width || char_count(text) > kMaxStringLength) {
        return std::nullopt;
    }
    const std::scoped_lock lock(impl_->mutex);
    Face* f = impl_->truetype(path, *width);
    return f != nullptr ? Impl::run(*f, text) : std::nullopt;
}

std::optional<std::int64_t> PillowFonts::length(const std::filesystem::path& path, double size,
                                                std::string_view text) {
    const auto width = request_width(size);
    if (!width || char_count(text) > kMaxStringLength) {
        return std::nullopt;
    }
    std::string key = std::to_string(*width);
    key += '\n';
    key += text;
    const std::scoped_lock lock(impl_->mutex);
    auto& cache = impl_->lengths[path];
    if (const auto it = cache.find(key); it != cache.end()) {
        return it->second;
    }
    Face* f = impl_->truetype(path, *width);
    const auto lay = f != nullptr ? Impl::run(*f, text) : std::nullopt;
    const std::optional<std::int64_t> out = lay ? std::optional<std::int64_t>(lay->length) : std::nullopt;
    if (cache.size() >= kMaxCached) {
        cache.clear();
    }
    cache.emplace(std::move(key), out);
    return out;
}

std::string PillowFonts::freetype_version() {
    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0) {
        return {};
    }
    FT_Int major = 0;
    FT_Int minor = 0;
    FT_Int patch = 0;
    FT_Library_Version(library, &major, &minor, &patch);
    FT_Done_FreeType(library);
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

} // namespace gc::render::detail
