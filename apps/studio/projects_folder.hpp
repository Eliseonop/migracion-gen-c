#pragma once

// Ajustes › Carpeta de los proyectos: la carpeta en la que el studio lee y guarda (data/projects.json,
// data/settings.json y clips/, como backend/ de video-yt). Se elige con el selector de carpetas y se
// cambia al momento, con el editor cerrado y sin nada a medias (una grabación de «Directo», una
// exportación); no se mueve nada, el studio pasa a abrir y guardar en la nueva. Se recuerda en
// studio.json, en la carpeta de configuración de la app; --root manda al arrancar.

#include <QObject>
#include <QString>
#include <QUrl>
#include <filesystem>
#include <optional>

#include "gc/core/projects_store.hpp"
#include "gc/core/settings.hpp"

namespace gc::studio {

class EditorModel;
class HomeModel;
class SettingsModel;

class ProjectsFolder : public QObject {
    Q_OBJECT
    // La carpeta en uso, con los separadores del sistema, y como URL (el selector y «Abrir la carpeta»).
    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(QUrl url READ url NOTIFY changed)
    Q_PROPERTY(QString defaultPath READ defaultPath CONSTANT)
    Q_PROPERTY(bool isDefault READ isDefault NOTIFY changed)
    // Se arrancó con --root y aún no se ha elegido otra.
    Q_PROPERTY(bool fromCommandLine READ fromCommandLine NOTIFY changed)
    // Cuántos proyectos tiene (los del inicio).
    Q_PROPERTY(int projectCount READ projectCount NOTIFY changed)
    // Por qué no se puede cambiar ahora («Cierra el proyecto…»); vacío si se puede.
    Q_PROPERTY(QString blocker READ blocker NOTIFY blockerChanged)
    // «Listo: …» al cambiarla.
    Q_PROPERTY(QString message READ message NOTIFY changed)
    // Lo que falló al cambiarla, o al arrancar, que la guardada ya no está.
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    // Con qué carpeta arranca el studio: la de --root, la guardada en `config` si sigue ahí o la de
    // siempre (y por qué no es la guardada, en `notice`).
    struct Start {
        std::filesystem::path root;
        QString notice;
        bool from_cli = false;
    };
    [[nodiscard]] static Start start(const QString& cli_root, const std::filesystem::path& config,
                                     const std::filesystem::path& default_root);
    // La carpeta guardada en `config` (studio.json), si hay.
    [[nodiscard]] static std::optional<std::filesystem::path> saved(const std::filesystem::path& config);

    // `store` y `settings` ya apuntan a la carpeta de arranque. `config` es studio.json y
    // `default_root`, la carpeta de siempre (la de datos de la app).
    ProjectsFolder(core::ProjectsStore& store, core::SettingsFile& settings, std::filesystem::path config,
                   std::filesystem::path default_root, QObject* parent = nullptr);

    // Lo que se recarga al cambiarla (el inicio y los ajustes) y quién lo impide (el editor). Pueden ser null.
    void setModels(HomeModel* home, SettingsModel* settings, EditorModel* editor);
    void setStart(const Start& start);

    [[nodiscard]] std::filesystem::path root() const;
    [[nodiscard]] QString path() const;
    [[nodiscard]] QUrl url() const;
    [[nodiscard]] QString defaultPath() const;
    [[nodiscard]] bool isDefault() const;
    [[nodiscard]] bool fromCommandLine() const { return from_cli_; }
    [[nodiscard]] int projectCount() const { return count_; }
    [[nodiscard]] QString blocker() const;
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] QString error() const { return error_; }

    // La carpeta del selector (URL file:/// o ruta). Si no tiene data/projects.json pero su backend/ sí
    // (la de video-yt), se usa backend/. false si no se puede, con el porqué en `error`.
    Q_INVOKABLE bool choose(const QString& folder);
    // Vuelve a la de siempre y olvida la guardada.
    Q_INVOKABLE bool useDefault();

signals:
    void changed();
    void blockerChanged();

private:
    bool switch_to(const std::filesystem::path& root);
    bool fail(const QString& why);
    void recount();

    core::ProjectsStore& store_;
    core::SettingsFile& settings_file_;
    std::filesystem::path config_;
    std::filesystem::path default_root_;
    HomeModel* home_ = nullptr;
    SettingsModel* settings_ = nullptr;
    EditorModel* editor_ = nullptr;
    bool from_cli_ = false;
    int count_ = 0;
    QString message_;
    QString error_;
};

} // namespace gc::studio
