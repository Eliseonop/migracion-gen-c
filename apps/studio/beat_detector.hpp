#pragma once

// «Detectar beats» del editor (detectClipBeats de VideoEditor.jsx y el POST
// /api/projects/{pid}/beats de main.py): los beats del audio del clip ENTERO (los tiempos son del
// archivo, no de la timeline), con gc::media::detect_beats en un hilo aparte. El editor los
// guarda en el clip ({times, bpm, every}). Sin gc_export (Linux sin vcpkg), no hay detección.

#include <QObject>
#include <QString>
#include <QVariantList>
#include <filesystem>
#include <memory>
#include <string>

#include "gc/core/model.hpp"

namespace gc::studio {

class BeatDetector : public QObject {
    Q_OBJECT

public:
    explicit BeatDetector(QObject* parent = nullptr);
    ~BeatDetector() override;
    BeatDetector(const BeatDetector&) = delete;
    BeatDetector& operator=(const BeatDetector&) = delete;
    BeatDetector(BeatDetector&&) = delete;
    BeatDetector& operator=(BeatDetector&&) = delete;

    [[nodiscard]] static bool available();
    // Empieza con una copia del clip; false si ya hay una en marcha o no hay detección. `root` es la
    // carpeta con data/ y clips/ (la del almacén).
    bool start(std::filesystem::path root, std::string project_id, core::TimelineClip clip);
    [[nodiscard]] bool running() const { return running_; }

signals:
    // Acabada: los tiempos (s del archivo) y el BPM; o el error, con el texto que ve el usuario.
    void finished(const QString& clipId, const QVariantList& times, double bpm, const QString& error);

private:
    struct Worker;
    void done(const QString& clipId, const QVariantList& times, double bpm, const QString& error);

    bool running_ = false;
    std::unique_ptr<Worker> worker_;
};

} // namespace gc::studio
