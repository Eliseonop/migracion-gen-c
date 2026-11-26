#include "projects_folder.hpp"

#include <QDir>
#include <QVariant>
#include <string>
#include <system_error>
#include <utility>

#include "gc/core/io.hpp"

#include "editor_model.hpp"
#include "export_job.hpp"
#include "home_model.hpp"
#include "live_recorder.hpp"
#include "settings_model.hpp"
#include "studio_config.hpp"

namespace gc::studio {

namespace {

namespace fs = std::filesystem;

constexpr const char* kRootKey = "root";

QString native(const fs::path& p) { return QDir::toNativeSeparators(QString::fromStdString(core::path_to_utf8(p))); }

// Una carpeta de la interfaz (o de --root), absoluta y limpia.
fs::path absolute(const QString& path) {
    return core::path_from_utf8(QDir::cleanPath(QDir(path).absolutePath()).toStdString());
}

bool same_folder(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    return fs::equivalent(a, b, ec) || a.lexically_normal() == b.lexically_normal();
}

// Guarda `root` en studio.json (nullopt la quita) y deja lo demás que haya.
Result<void> remember(const fs::path& config, const std::optional<fs::path>& root) {
    return studio_config::set(config, kRootKey,
                              root ? std::optional<std::string>(core::path_to_utf8(*root)) : std::nullopt);
}

} // namespace

ProjectsFolder::Start ProjectsFolder::start(const QString& cli_root, const fs::path& config,
                                            const fs::path& default_root) {
    if (!cli_root.isEmpty()) {
        return {absolute(cli_root), {}, true};
    }
    if (const auto root = saved(config)) {
        std::error_code ec;
        if (fs::is_directory(*root, ec)) {
            return {*root, {}, false};
        }
        // Un disco que no está, una carpeta movida: se arranca con la de siempre y se vuelve a
        // probar la próxima vez.
        return {default_root, QString("No encuentro «%1»: abro la carpeta de siempre hasta que vuelva.").arg(native(*root)),
                false};
    }
    return {default_root, {}, false};
}

std::optional<fs::path> ProjectsFolder::saved(const fs::path& config) {
    if (const auto root = studio_config::get(config, kRootKey)) {
        return core::path_from_utf8(*root);
    }
    return std::nullopt;
}

ProjectsFolder::ProjectsFolder(core::ProjectsStore& store, core::SettingsFile& settings, fs::path config,
                               fs::path default_root, QObject* parent)
    : QObject(parent), store_(store), settings_file_(settings), config_(std::move(config)),
      default_root_(std::move(default_root)) {
    recount();
}

void ProjectsFolder::setModels(HomeModel* home, SettingsModel* settings, EditorModel* editor) {
    home_ = home;
    settings_ = settings;
    editor_ = editor;
    if (home_ != nullptr) {
        connect(home_, &HomeModel::changed, this, &ProjectsFolder::recount);
    }
    if (editor_ != nullptr) {
        connect(editor_, &EditorModel::projectChanged, this, &ProjectsFolder::blockerChanged);
        connect(&editor_->liveRecorder(), &LiveRecorder::stateChanged, this, &ProjectsFolder::blockerChanged);
        connect(&editor_->exportJob(), &ExportJob::changed, this, &ProjectsFolder::blockerChanged);
    }
    recount();
    emit blockerChanged();
}

void ProjectsFolder::setStart(const Start& start) {
    from_cli_ = start.from_cli;
    error_ = start.notice;
    emit changed();
}

fs::path ProjectsFolder::root() const { return store_.clips_dir().parent_path(); }

QString ProjectsFolder::path() const { return native(root()); }

QUrl ProjectsFolder::url() const { return QUrl::fromLocalFile(QString::fromStdString(core::path_to_utf8(root()))); }

QString ProjectsFolder::defaultPath() const { return native(default_root_); }

bool ProjectsFolder::isDefault() const { return same_folder(root(), default_root_); }

QString ProjectsFolder::blocker() const {
    if (editor_ == nullptr) {
        return {};
    }
    if (editor_->isOpen()) {
        return QStringLiteral("Cierra el proyecto para cambiar de carpeta.");
    }
    // Las dos siguen con el editor cerrado y acaban guardando en la carpeta del proyecto.
    if (editor_->liveRecorder().busy()) {
        return QStringLiteral("Espera a que acabe la grabación del directo.");
    }
    if (editor_->exportJob().running()) {
        return QStringLiteral("Espera a que acabe la exportación.");
    }
    return {};
}

bool ProjectsFolder::choose(const QString& folder) {
    const QUrl url(folder);
    const QString local = url.isLocalFile() ? url.toLocalFile() : folder.trimmed();
    if (local.isEmpty()) {
        return fail(QStringLiteral("Elige una carpeta."));
    }
    fs::path root = absolute(local);
    std::error_code ec;
    if (!fs::exists(root / "data" / "projects.json", ec) && fs::exists(root / "backend" / "data" / "projects.json", ec)) {
        root /= "backend"; // la carpeta de video-yt: sus proyectos están en backend/
    }
    return switch_to(root);
}

bool ProjectsFolder::useDefault() { return switch_to(default_root_); }

bool ProjectsFolder::switch_to(const fs::path& root) {
    if (const QString why = blocker(); !why.isEmpty()) {
        return fail(why);
    }
    std::error_code ec;
    fs::create_directories(root / "data", ec); // ahí van projects.json y settings.json
    if (!fs::is_directory(root / "data", ec)) {
        return fail(QString("No se puede guardar en «%1».").arg(native(root)));
    }
    const bool same = same_folder(root, this->root());
    if (!same) {
        store_.relocate(root / "data" / "projects.json", root / "clips");
        settings_file_.relocate(root / "data" / "settings.json");
        if (settings_ != nullptr) {
            settings_->reload();
        }
        if (home_ != nullptr) {
            home_->openGroup({}); // la raíz: las carpetas del inicio son las de la otra
        }
    }
    const bool to_default = same_folder(root, default_root_);
    const auto remembered = remember(config_, to_default ? std::nullopt : std::optional<fs::path>(root));
    error_ = remembered ? QString()
                        : QString("Se usa, pero no se pudo recordar para la próxima vez: %1")
                              .arg(QString::fromStdString(remembered.error().message));
    if (!same) {
        message_ = QString("Listo: GenClipPro abre y guarda ahora los proyectos de «%1».").arg(native(root));
    } else if (from_cli_ && !to_default) {
        message_ = QStringLiteral("Recordada: GenClipPro la abrirá también sin --root.");
    } else {
        message_ = QStringLiteral("Ya estás usando esa carpeta.");
    }
    from_cli_ = false;
    recount();
    emit changed();
    return true;
}

bool ProjectsFolder::fail(const QString& why) {
    error_ = why;
    message_.clear();
    emit changed();
    return false;
}

void ProjectsFolder::recount() {
    int n = 0;
    if (home_ != nullptr) { // un group_id que no existe cuenta en la raíz: raíz + carpetas son todos
        n = home_->rootCount();
        for (const QVariant& g : home_->groups()) {
            n += g.toMap().value("count").toInt();
        }
    } else if (const auto list = store_.list_projects()) {
        n = static_cast<int>(list->size());
    }
    if (n != count_) {
        count_ = n;
        emit changed();
    }
}

} // namespace gc::studio
