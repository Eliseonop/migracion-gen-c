#pragma once

// Las miniaturas del inicio (Home.jsx): la tarjeta de un proyecto enseña el primer fotograma de su
// primer clip, el `<video src={p.clips[0].url} preload="metadata">` que el navegador pinta en el
// recuadro de la tarjeta con `object-fit: cover`; sin clips, el icono. Aquí ese fotograma sale de
// gc_media (gc::media::frame_at en 0 s, con el autogiro, como el navegador), se reduce y se recorta
// al centro a la proporción del recuadro de ProjectCard.qml (lo que hace `object-fit: cover`) y se
// guarda en una carpeta de caché de la app: <caché>/<clave>.jpg, con la clave sacada de la ruta, el
// tamaño y la fecha del vídeo (como la de los proxies), así que un archivo cambiado da otra. Un hilo
// de baja prioridad mira la caché y hace las que faltan, una a una, sin frenar la interfaz. Sin
// gc_export (Linux sin vcpkg), no hay miniaturas y la tarjeta se queda con su icono.
//
// Divergencias: video-yt no guarda nada (el navegador vuelve a leer el vídeo cada vez que pinta el
// inicio), y con el archivo del clip perdido o roto su recuadro queda vacío; aquí, el icono. Las
// miniaturas viejas (de un archivo que cambió) se quedan en la caché, como los proxies.

#include <QObject>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "gc/core/error.hpp"

namespace gc::studio {

// El recuadro de la miniatura en ProjectCard.qml (220 × 124) al doble, para pantallas de 2×.
inline constexpr int kThumbWidth = 440;
inline constexpr int kThumbHeight = 248;

// La miniatura de `source` que ya está en la caché (la del archivo tal como está ahora), o nada (sin
// caché, sin hacer o si el vídeo no se puede leer).
[[nodiscard]] std::optional<std::filesystem::path> find_thumbnail(const std::filesystem::path& cache_dir,
                                                                  const std::filesystem::path& source);

// La hace y la deja en la caché (o da la que ya hay): el primer fotograma, de kThumbWidth ×
// kThumbHeight, en JPEG. Se escribe en <clave>.jpg.part y se renombra al acabar: una a medias nunca
// se usa. Error si el vídeo no se puede leer o no tiene fotogramas, o si no se puede escribir; sin
// gc_export, siempre.
[[nodiscard]] Result<std::filesystem::path> make_thumbnail(const std::filesystem::path& cache_dir,
                                                           const std::filesystem::path& source);

class Thumbnails : public QObject {
    Q_OBJECT

public:
    explicit Thumbnails(QObject* parent = nullptr);
    ~Thumbnails() override;
    Thumbnails(const Thumbnails&) = delete;
    Thumbnails& operator=(const Thumbnails&) = delete;
    Thumbnails(Thumbnails&&) = delete;
    Thumbnails& operator=(Thumbnails&&) = delete;

    // La carpeta de la caché (la de la app); vacía, sin miniaturas.
    void setCacheDir(std::filesystem::path dir);
    [[nodiscard]] const std::filesystem::path& cacheDir() const { return cache_dir_; }

    // Los vídeos que necesitan miniatura, en el orden de las tarjetas: el hilo mira primero la caché
    // (las que ya están llegan juntas) y luego hace las que faltan, una a una. Una petición nueva
    // sustituye a lo que quedaba de la anterior (la que se está haciendo se acaba).
    void request(std::vector<std::filesystem::path> sources);

    // La miniatura de un vídeo, si ya se sabe (vacía si no).
    [[nodiscard]] std::filesystem::path find(const std::filesystem::path& source) const;

signals:
    void changed();

private:
    struct Worker;
    // Vídeo → su miniatura (vacía: no tiene).
    using Done = std::vector<std::pair<std::filesystem::path, std::filesystem::path>>;
    void publish(const Done& done);

    std::filesystem::path cache_dir_;
    std::map<std::filesystem::path, std::filesystem::path> ready_;
    std::unique_ptr<Worker> worker_; // el último: su hilo se para antes que lo demás
};

} // namespace gc::studio
