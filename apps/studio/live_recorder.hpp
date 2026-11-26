#pragma once

// «Directo» (Vídeo › Directo del material; es de GenClipPro, video-yt no lo tiene): graba el directo de un enlace de
// TikTok o de YouTube y marca momentos mientras graba. yt-dlp saca la dirección del flujo (`-J`, sin bajar nada) y
// FFmpeg lo graba sin recomprimir en MPEG-TS en <proyecto>/live (gc/core/live_capture.hpp); «Parar» le manda «q».
// Después el TS pasa a MP4 y `recorded` lo entrega: EditorModel lo mete en el material con un clip por marca y llama
// a `finish`. Si el directo acaba solo, se guarda igual.
//
// Los programas: FFmpeg, GENCLIP_FFMPEG o el del PATH. yt-dlp, lo que dice findYtdlp: también el que se elige en el
// panel (se recuerda en studio.json), el de winget y la venv de video-yt si la carpeta de los proyectos es la suya.

#include <filesystem>
#include <optional>

#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
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
    // El yt-dlp con el que se buscan los directos (con los separadores del sistema); vacío si no hay ninguno.
    Q_PROPERTY(QString ytdlpPath READ ytdlpPath NOTIFY toolsChanged)

public:
    // Dónde se busca yt-dlp, por orden: la variable GENCLIP_YTDLP, el elegido en el panel (si sigue ahí), las carpetas
    // del PATH, winget (en LOCALAPPDATA: lo que se instala con el studio abierto no está en su PATH) y la venv de la
    // carpeta de los proyectos o la de su padre (<video-yt>/.venv, con la carpeta en su backend/).
    struct YtdlpPlaces {
        QString env;
        std::optional<std::filesystem::path> chosen;
        QStringList path;
        std::filesystem::path local_app_data;
        std::filesystem::path data_root;
    };
    // El primero que existe, o vacío.
    [[nodiscard]] static QString findYtdlp(const YtdlpPlaces& places);

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
    // La carpeta de datos (la de data/projects.json): su venv es uno de los sitios donde se busca yt-dlp.
    void setDataRoot(const std::filesystem::path& root);
    // studio.json: ahí se recuerda el yt-dlp elegido (clave «ytdlp»).
    void setConfig(const std::filesystem::path& config);
    // Para los tests: el entorno en el que se busca yt-dlp (GENCLIP_YTDLP, PATH y LOCALAPPDATA), en vez del sistema.
    void setEnvironment(const QProcessEnvironment& env);
    // Para los tests: los programas (el primero es el ejecutable; los demás van delante de los argumentos).
    void setTools(const QStringList& ytdlp, const QStringList& ffmpeg);
    [[nodiscard]] QString ytdlpPath() const;

    // El yt-dlp que elige el usuario (la URL file:/// del selector o una ruta): se usa desde ya y se recuerda. false,
    // con el porqué en `error`, si no es un archivo.
    Q_INVOKABLE bool chooseYtdlp(const QString& file);
    // Vuelve a buscar yt-dlp (al volver al panel: puede que se acabe de instalar).
    Q_INVOKABLE void refreshTools() { emit toolsChanged(); }

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
    void toolsChanged();
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
    std::optional<QProcessEnvironment> env_override_;
    std::filesystem::path data_root_;
    std::filesystem::path config_;                     // studio.json
    std::optional<std::filesystem::path> chosen_ytdlp_; // el elegido en el panel
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
