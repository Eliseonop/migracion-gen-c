#pragma once

// Ajustes › Versión: buscar e instalar una versión nueva de GenClipPro (es de GenClipPro: video-yt no lo tiene).
// Solo en la de producción, la instalada desde el .zip de una release: lleva genclip-release.json junto al .exe
// (tools/release/empaquetar.ps1). La de desarrollo (la build, «GenClipPro Dev» del menú Inicio) es siempre la última
// que se compila y no se actualiza.
//
// Busca primero en las releases locales (los .zip que deja empaquetar.ps1 en <AppLocalData>/releases: la «release
// local») y luego en la última release de GitHub (sin token: el repositorio es público). «Actualizar» comprueba el
// .zip con su .sha256 (bajándolo antes si es de GitHub; sin él no se instala), escribe un script de PowerShell que
// espera a que se cierre la app, pone la carpeta nueva en el sitio de la instalada y la abre, lo lanza y cierra la app.

#include <QByteArray>
#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <compare>
#include <filesystem>
#include <memory>
#include <optional>

class QNetworkReply;

namespace gc::studio {

class Updater : public QObject {
    Q_OBJECT
    // La versión de esta app (gc::kVersion).
    Q_PROPERTY(QString version READ version CONSTANT)
    // La de producción (instalada desde una release: se puede actualizar) o la de desarrollo.
    Q_PROPERTY(bool installed READ installed CONSTANT)
    // «» (sin buscar), «checking», «uptodate», «available», «downloading», «installing» o «error».
    Q_PROPERTY(QString status READ status NOTIFY changed)
    // La versión nueva y de dónde viene: «local» (una release de este PC) o «github».
    Q_PROPERTY(QString latest READ latest NOTIFY changed)
    Q_PROPERTY(QString source READ source NOTIFY changed)
    // Lo bajado de la de GitHub, de 0 a 1.
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    struct Version {
        int major = 0;
        int minor = 0;
        int patch = 0;
        auto operator<=>(const Version&) const = default;
    };
    // «0.23.0» o «v0.23.0»; nada si no es una versión de tres números.
    [[nodiscard]] static std::optional<Version> parseVersion(const QString& text);

    struct Release {
        Version version;
        QString text;   // «0.24.0»
        QString source; // «local» o «github»
        QUrl zip;       // el .zip (un archivo, si es local)
        QUrl sha256;    // su .sha256 (vacía si no hay)
    };
    // La release local más nueva que `current` en `dir` (los GenClipPro-<versión>-win64.zip).
    [[nodiscard]] static std::optional<Release> newestLocal(const std::filesystem::path& dir, const Version& current);
    // La del JSON de /releases/latest de GitHub, si es más nueva que `current` y trae el .zip de Windows.
    [[nodiscard]] static std::optional<Release> fromGithub(const QByteArray& json, const Version& current);
    // El script de PowerShell que cambia la instalación (parámetros -AppPid, -Zip, -Install y -Log).
    [[nodiscard]] static QString updateScript();

    explicit Updater(QObject* parent = nullptr);
    ~Updater() override;
    Updater(const Updater&) = delete;
    Updater& operator=(const Updater&) = delete;
    Updater(Updater&&) = delete;
    Updater& operator=(Updater&&) = delete;

    // Para los tests: la carpeta de la app (donde se mira genclip-release.json y la que se cambia), la de las
    // releases locales, la URL de la API (en vez de api.github.com) y no lanzar el script ni cerrar la app.
    void setAppDir(const std::filesystem::path& dir);
    void setLocalReleases(const std::filesystem::path& dir);
    void setApiUrl(const QUrl& url);
    void setDryRun(bool dry) { dry_run_ = dry; }

    [[nodiscard]] QString version() const;
    [[nodiscard]] bool installed() const;
    [[nodiscard]] QString status() const { return status_; }
    [[nodiscard]] QString latest() const { return found_ ? found_->text : QString(); }
    [[nodiscard]] QString source() const { return found_ ? found_->source : QString(); }
    [[nodiscard]] double progress() const { return progress_; }
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] QString error() const { return error_; }

    // Busca una versión nueva (en este PC y en GitHub).
    Q_INVOKABLE void check();
    // Pone la versión nueva que encontró check(): solo en la de producción.
    Q_INVOKABLE void update();

signals:
    void changed();
    // El script, listo con sus argumentos: se lanza y la app se cierra (en los tests, solo la señal).
    void readyToRestart(const QString& script, const QStringList& args);

private:
    void set(const QString& status, const QString& message = {}, const QString& error = {});
    void checked(QNetworkReply* reply);
    void install(const QString& zip, const QByteArray& expected_sha);
    void download();

    QNetworkAccessManager net_;
    std::filesystem::path app_dir_;
    std::filesystem::path local_dir_;
    QUrl api_url_;
    bool dry_run_ = false;
    QString status_;
    QString message_;
    QString error_;
    double progress_ = 0;
    std::optional<Release> found_;
    QPointer<QNetworkReply> reply_;
    std::unique_ptr<QFile> file_; // el .zip que se baja de GitHub
    QByteArray sha_;              // el .sha256 de la de GitHub
};

} // namespace gc::studio
