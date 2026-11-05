#pragma once

// El export desde el editor (el botón Exportar), como el job de export de video-yt
// (jobs._run_export): la timeline de la sesión renderizada con gc::exporter::render_project a
// <carpeta del proyecto>/exports/final_<AAAAMMDD-HHMMSS en UTC>.mp4, en un hilo aparte y con su
// progreso. Lo que el render aún no sabe dibujar (los motion graphics, que video-yt renderiza antes
// con su motion service) se salta con un aviso en vez de parar el export: los avisos quedan en
// `warnings`. Se puede cancelar: el MP4 a medias se borra. Sin gc_export, `available` es false.

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <filesystem>
#include <memory>
#include <string>

#include "gc/core/session.hpp"

namespace gc::studio {

class ExportJob : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed) // 0–1
    // «Exportando… 45 %», «Vídeo final exportado.», «Export cancelado.» o el error.
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString file READ file NOTIFY changed) // el MP4 hecho
    Q_PROPERTY(QUrl folderUrl READ folderUrl NOTIFY changed)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY changed)

public:
    explicit ExportJob(QObject* parent = nullptr);
    ~ExportJob() override; // cancela y espera al hilo
    ExportJob(const ExportJob&) = delete;
    ExportJob& operator=(const ExportJob&) = delete;
    ExportJob(ExportJob&&) = delete;
    ExportJob& operator=(ExportJob&&) = delete;

    [[nodiscard]] bool available() const;
    [[nodiscard]] bool running() const { return running_; }
    [[nodiscard]] double progress() const { return progress_; }
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] QString error() const { return error_; }
    [[nodiscard]] QString file() const { return file_; }
    [[nodiscard]] QUrl folderUrl() const { return folder_; }
    [[nodiscard]] QStringList warnings() const { return warnings_; }

    // Exporta `timeline` del proyecto `project_id` de `root` (la carpeta de data/projects.json y
    // clips/). false si ya hay uno en marcha, si la timeline está vacía (el error lo dice) o sin
    // gc_export.
    bool start(std::filesystem::path root, std::string project_id, core::TimelineSnapshot timeline);
    Q_INVOKABLE void cancel();
    // Olvida el resultado que se enseña (el export hecho, el error o la cancelación).
    Q_INVOKABLE void dismiss();

signals:
    void changed();
    void finished(); // acabó (bien, mal o cancelado)

private:
    struct Worker;
    void progressed(double progress);
    void done(const QString& file, const QString& error, bool cancelled, const QStringList& warnings);

    bool running_ = false;
    double progress_ = 0;
    QString message_;
    QString error_;
    QString file_;
    QUrl folder_;
    QStringList warnings_;
    std::unique_ptr<Worker> worker_; // el último: su hilo se para antes que lo demás
};

} // namespace gc::studio
