#pragma once

// La vista previa del editor: el fotograma de la timeline en el cabezal, el mismo que compone el
// export en ese fotograma (gc::exporter::FrameRenderer), hecho en un hilo aparte para no frenar la
// interfaz. Se queda con la última petición: si llegan varias mientras compone (el cabezal
// corriendo), salta a la más nueva. Cuando cambia la timeline vuelve a preparar el render y repite
// el último instante pedido. Sin gc_export (Linux sin vcpkg), `available` es false y no hace nada.

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <filesystem>
#include <memory>
#include <string>

#include "gc/core/session.hpp"

namespace gc::studio {

class PreviewSource : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(int revision READ revision NOTIFY frameChanged)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY frameChanged)
    Q_PROPERTY(QString error READ error NOTIFY frameChanged)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY frameChanged)

public:
    explicit PreviewSource(QObject* parent = nullptr);
    ~PreviewSource() override;
    PreviewSource(const PreviewSource&) = delete;
    PreviewSource& operator=(const PreviewSource&) = delete;
    PreviewSource(PreviewSource&&) = delete;
    PreviewSource& operator=(PreviewSource&&) = delete;

    // ¿Se compiló con gc_export?
    [[nodiscard]] bool available() const;
    // Sube con cada fotograma (o error) publicado.
    [[nodiscard]] int revision() const { return revision_; }
    [[nodiscard]] bool hasFrame() const { return !image_.isNull(); }
    // Por qué no hay fotograma (timeline vacía, material que no se lee…); vacío si lo hay.
    [[nodiscard]] QString error() const { return error_; }
    // Lo que el render se salta (material que falta, lo que aún no sabe dibujar), cada aviso una vez.
    [[nodiscard]] QStringList warnings() const { return warnings_; }
    // El último fotograma: RGBA del tamaño de la salida (par, como el export); nulo si no hay.
    [[nodiscard]] QImage image() const { return image_; }
    // Su tick (fotograma k de la timeline, en k/fps s); -1 sin fotograma.
    [[nodiscard]] qint64 tick() const { return tick_; }

    // El proyecto `project_id` de la carpeta `root` (la de data/projects.json y clips/, como la
    // raíz de video-yt): de ahí salen el material y los ajustes.
    void open(std::filesystem::path root, std::string project_id);
    void close();
    // La timeline que se dibuja (la de la sesión de edición, sin esperar a que se guarde).
    void setTimeline(core::TimelineSnapshot timeline);
    // El fotograma en `seconds` (el más cercano de la rejilla de 1/fps, dentro de la timeline).
    void request(double seconds);

signals:
    void frameChanged();

private:
    struct Worker;
    void publish(const QImage& image, qint64 tick, const QString& error, const QStringList& warnings);

    core::TimelineSnapshot sent_; // la última timeline que se mandó al hilo
    QImage image_;
    qint64 tick_ = -1;
    QString error_;
    QStringList warnings_;
    int revision_ = 0;
    std::unique_ptr<Worker> worker_; // el último: su hilo se para antes de soltar lo demás
};

} // namespace gc::studio
