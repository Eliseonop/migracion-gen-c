#pragma once

// Un fotograma de un vídeo en un instante, decodificado en un hilo: lo que el modal Recortar enseña
// de un vídeo (en EdCropModal, el <video> oculto del editor llevado al instante del cabezal). Si
// llegan varios pedidos mientras decodifica, solo cuenta el último. Sin gc_media, nada.

#include <QImage>
#include <QObject>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace gc::studio {

class FrameGrabber : public QObject {
    Q_OBJECT

public:
    explicit FrameGrabber(QObject* parent = nullptr);
    ~FrameGrabber() override;
    FrameGrabber(const FrameGrabber&) = delete;
    FrameGrabber& operator=(const FrameGrabber&) = delete;
    FrameGrabber(FrameGrabber&&) = delete;
    FrameGrabber& operator=(FrameGrabber&&) = delete;

    // El fotograma de `path` en `t` (s del archivo), acotado como safeMediaTime: no más allá de la
    // duración menos un fotograma a `fps` (o 0,04 s). Sustituye al que estuviera pendiente.
    void request(std::filesystem::path path, double t, double fps);
    // Olvida lo pedido y lo que había: lo que esté decodificando ya no llega.
    void clear();
    // El último que llegó (RGBA, con el autogiro de un vídeo de móvil, como lo dibuja el render).
    [[nodiscard]] const QImage& frame() const { return frame_; }

signals:
    void frameChanged();

private:
    struct Worker;
    void deliver(std::uint64_t asked, QImage image);

    std::unique_ptr<Worker> worker_;
    QImage frame_;
    std::uint64_t asked_ = 0; // el último pedido: lo que llegue de uno anterior no cuenta
};

} // namespace gc::studio
