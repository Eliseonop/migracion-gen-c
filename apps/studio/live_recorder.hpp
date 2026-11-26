#pragma once

// «Directo» (Vídeo › Directo del material; es de GenClipPro, video-yt no lo tiene): graba el directo de un enlace de
// TikTok o de YouTube y marca momentos mientras graba. yt-dlp saca la dirección del flujo (`-J`, sin bajar nada) y
// FFmpeg lo graba sin recomprimir en MPEG-TS en <proyecto>/live (gc/core/live_capture.hpp); «Parar» le manda «q».
// Después el TS pasa a MP4 y `recorded` lo entrega: EditorModel lo mete en el material con un clip por marca y llama
// a `finish`. Si el directo acaba solo, se guarda igual.
//
// Los programas: GENCLIP_YTDLP y GENCLIP_FFMPEG, si no el PATH y, para yt-dlp, la venv de video-yt si la carpeta de
// datos es su backend/ (<video-yt>/.venv/Scripts/yt-dlp.exe).

#include <filesystem>

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include "gc/core/live_capture.hpp"

namespace gc::studio {

class LiveRecorder : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString url READ url WRITE setUrl NOTIFY urlChanged)
    // «» (nada), «resolving» (yt-dlp busca el directo), «recording», «saving» (parar y pasar a MP4), «done» o «error».
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    // {platform, title, uploader}; vacío hasta que yt-dlp encuentra el directo.
    Q_PROPERTY(QVariantMap info READ info NOTIFY stateChanged)
    // Lo grabado: segundos y bytes (el -progress de FFmpeg).
    Q_PROPERTY(double seconds READ seconds NOTIFY progressChanged)
    Q_PROPERTY(qint64 bytes READ bytes NOTIFY progressChanged)
    // Las marcas: [{t, label}], con t en segundos de la grabación y label «m:ss».
    Q_PROPERTY(QVariantList marks READ marks NOTIFY marksChanged)
    // Lo que se guarda de cada marca: los segundos de antes y los de después.
    Q_PROPERTY(double before READ before WRITE setBefore NOTIFY windowChanged)
    Q_PROPERTY(double after READ after WRITE setAfter NOTIFY windowChanged)

public:
    explicit LiveRecorder(QObject* parent = nullptr);
    ~LiveRecorder() override;
    LiveRecorder(const LiveRecorder&) = delete;
    LiveRecorder& operator=(const LiveRecorder&) = delete;
    LiveRecorder(LiveRecorder&&) = delete;
    LiveRecorder& operator=(LiveRecorder&&) = delete;

    [[nodiscard]] QString url() const { return url_; }
    void setUrl(const QString& url);
    [[nodiscard]] QString status() const { return status_; }
    [[nodiscard]] bool busy() const;
    [[nodiscard]] QString error() const { return error_; }
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] QVariantMap info() const { return info_; }
    [[nodiscard]] double seconds() const { return progress_.seconds; }
    [[nodiscard]] qint64 bytes() const { return progress_.bytes; }
    [[nodiscard]] QVariantList marks() const { return marks_; }
    [[nodiscard]] double before() const { return before_; }
    void setBefore(double seconds);
    [[nodiscard]] double after() const { return after_; }
    void setAfter(double seconds);

    // El proyecto abierto y su carpeta de grabaciones; una grabación en marcha sigue con el suyo.
    void setProject(const QString& pid, const std::filesystem::path& folder);
    // La carpeta de datos (la de data/projects.json): para encontrar la venv de video-yt.
    void setDataRoot(const std::filesystem::path& root) { data_root_ = root; }
    // Para los tests: los programas (el primero es el ejecutable; los demás van delante de los argumentos).
    void setTools(const QStringList& ytdlp, const QStringList& ffmpeg);

    // Busca el directo del enlace (o el de `url`, que pasa al campo) y empieza a grabar.
    Q_INVOKABLE void start(const QString& url = {});
    // Marca el momento actual de la grabación.
    Q_INVOKABLE void mark();
    Q_INVOKABLE void removeMark(int index);
    // Para y guarda; mientras busca el directo, lo deja.
    Q_INVOKABLE void stop();
    // Para y tira lo grabado.
    Q_INVOKABLE void cancel();
    // Tras acabar (bien o con error): el panel vuelve a empezar, con el enlace.
    Q_INVOKABLE void reset();
    // EditorModel, con la grabación ya en el material: el mensaje final, o el error.
    void finish(const QString& message, const QString& error = {});

signals:
    void urlChanged();
    void stateChanged();
    void progressChanged();
    void marksChanged();
    void windowChanged();
    // El MP4 listo para el material: `info` lleva {pid, platform, url, id, title, uploader, ended} (ended: el directo
    // acabó solo) y `marks` las de la grabación.
    void recorded(const QString& file, const QVariantMap& info, const QVariantList& marks, double seconds);

private:
    void set_status(const QString& status);
    void fail(const QString& error);
    void resolved(int code);
    void read_progress();
    void recording_ended(int code);
    void remuxed(int code);
    void remove_files();
    [[nodiscard]] QStringList ytdlp() const;
    [[nodiscard]] QStringList ffmpeg() const;
    void run(QProcess& process, const QStringList& tool, const std::vector<std::string>& args);

    QProcess resolver_;
    QProcess recorder_;
    QProcess remuxer_;
    QTimer stop_timeout_; // si FFmpeg no para con la «q», se le mata
    QStringList ytdlp_override_;
    QStringList ffmpeg_override_;
    std::filesystem::path data_root_;
    QString project_;
    std::filesystem::path folder_; // la del proyecto abierto
    QString url_;
    QString status_;
    QString error_;
    QString message_;
    QVariantMap info_;
    QVariantList marks_;
    core::LiveProgress progress_;
    double before_ = 30;
    double after_ = 10;
    // La grabación en curso: su proyecto, sus archivos y lo que FFmpeg deja en stderr (el final).
    QString pid_;
    std::filesystem::path ts_;
    std::filesystem::path mp4_;
    QByteArray stderr_;
    bool ended_ = false; // el directo acabó solo
};

} // namespace gc::studio
