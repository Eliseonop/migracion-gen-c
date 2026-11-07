#pragma once

// Los proxies del proyecto abierto (gc/export/proxy.hpp): un hilo de baja prioridad mira los vídeos
// de la timeline, coge de la caché los proxies que ya hay y hace, uno a uno, los que faltan (los
// vídeos de 1440p o más). Cada uno que acaba va a la vista previa, que desde entonces lee ese vídeo
// del proxy. Un cambio de la timeline no corta el que se está haciendo; cerrar o abrir otro
// proyecto, sí (y no deja nada a medias). Al abrir un proyecto y al hacer proxies nuevos, la caché
// se limpia hasta su tope (exporter::prune_proxies, sin tocar los de este proyecto). Sin gc_export
// (Linux sin vcpkg), no hace nada.

#include <QObject>
#include <QString>
#include <filesystem>
#include <memory>
#include <string>

#include "gc/core/session.hpp"

#include "preview_source.hpp"

namespace gc::studio {

class ProxyMaker : public QObject {
    Q_OBJECT
    // «Preparando proxies: 1 de 3 (45 %)», el error del último que falló, o vacío.
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(int ready READ ready NOTIFY proxiesChanged)

public:
    explicit ProxyMaker(QObject* parent = nullptr);
    ~ProxyMaker() override;
    ProxyMaker(const ProxyMaker&) = delete;
    ProxyMaker& operator=(const ProxyMaker&) = delete;
    ProxyMaker(ProxyMaker&&) = delete;
    ProxyMaker& operator=(ProxyMaker&&) = delete;

    // La carpeta de la caché (la de la app); vacía, sin proxies.
    void setCacheDir(std::filesystem::path dir);
    [[nodiscard]] const std::filesystem::path& cacheDir() const { return cache_dir_; }

    // Como a PreviewSource: el proyecto (de él sale la ruta de cada vídeo) y su timeline.
    void open(std::filesystem::path root, std::string project_id);
    void close();
    void setTimeline(core::TimelineSnapshot timeline);

    // Los hechos, por la ruta del original.
    [[nodiscard]] const PreviewProxies& proxies() const { return proxies_; }
    [[nodiscard]] int ready() const { return static_cast<int>(proxies_.size()); }
    [[nodiscard]] QString status() const { return status_; }

signals:
    void proxiesChanged();
    void statusChanged();

private:
    struct Worker;
    void publish(const PreviewProxies& proxies, const QString& status);

    std::filesystem::path cache_dir_;
    core::TimelineSnapshot sent_; // la última timeline que se mandó al hilo
    PreviewProxies proxies_;
    QString status_;
    std::unique_ptr<Worker> worker_; // el último: su hilo se para antes que lo demás
};

} // namespace gc::studio
