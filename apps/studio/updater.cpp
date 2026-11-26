#include "updater.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <system_error>

#include "gc/core/io.hpp"
#include "gc/core/version.hpp"

namespace gc::studio {

namespace {

namespace fs = std::filesystem;

// Junto al .exe de una instalación de producción (lo escribe tools/release/empaquetar.ps1).
constexpr const char* kMarker = "genclip-release.json";
// De dónde salen las releases si el marcador no dice otro.
constexpr const char* kRepo = "Eliseonop/migracion-gen-c";
constexpr const char* kAgent = "GenClipPro";

QString qpath(const fs::path& p) { return QString::fromStdString(core::path_to_utf8(p)); }
fs::path to_path(const QString& s) { return core::path_from_utf8(s.toStdString()); }

QString current_text() { return QString::fromUtf8(gc::kVersion.data(), static_cast<qsizetype>(gc::kVersion.size())); }

// GenClipPro-0.23.0-win64.zip
QString zip_name(const QString& version) { return QString("GenClipPro-%1-win64.zip").arg(version); }

// El hash de un .sha256 («<hex>  <nombre>» o solo el hex), en minúsculas; vacío si no lo es.
QByteArray sha_from_text(QByteArray text) {
    if (text.startsWith("\xEF\xBB\xBF")) {
        text.remove(0, 3); // el BOM de UTF-8 (lo pone el -Encoding UTF8 de Windows PowerShell 5.1)
    }
    const QByteArray first = text.simplified().split(' ').value(0).toLower();
    static const QRegularExpression hex(QStringLiteral("^[0-9a-f]{64}$"));
    return hex.match(QString::fromLatin1(first)).hasMatch() ? first : QByteArray();
}

QByteArray file_sha256(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&f) ? hash.result().toHex() : QByteArray();
}

QString update_dir() {
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("GenClipPro-update");
    QDir().mkpath(dir);
    return dir;
}

QNetworkRequest request(const QUrl& url, int timeout_ms) {
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", kAgent); // la API de GitHub lo pide
    req.setTransferTimeout(timeout_ms);
    return req;
}

} // namespace

std::optional<Updater::Version> Updater::parseVersion(const QString& text) {
    static const QRegularExpression re(QStringLiteral("^v?(\\d+)\\.(\\d+)\\.(\\d+)$"));
    const auto m = re.match(text.trimmed());
    if (!m.hasMatch()) {
        return std::nullopt;
    }
    return Version{m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()};
}

std::optional<Updater::Release> Updater::newestLocal(const fs::path& dir, const Version& current) {
    static const QRegularExpression re(QStringLiteral("^GenClipPro-(\\d+\\.\\d+\\.\\d+)-win64\\.zip$"));
    std::optional<Release> best;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const auto m = re.match(qpath(it->path().filename()));
        const auto v = m.hasMatch() ? parseVersion(m.captured(1)) : std::nullopt;
        if (!v || *v <= current || (best && *v <= best->version) || !it->is_regular_file(ec)) {
            continue;
        }
        fs::path sha = it->path();
        sha += ".sha256";
        best = Release{*v, m.captured(1), "local", QUrl::fromLocalFile(qpath(it->path())),
                       fs::is_regular_file(sha, ec) ? QUrl::fromLocalFile(qpath(sha)) : QUrl()};
    }
    return best;
}

std::optional<Updater::Release> Updater::fromGithub(const QByteArray& json, const Version& current) {
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    const QString tag = o.value("tag_name").toString();
    const auto v = parseVersion(tag);
    if (!v || *v <= current || o.value("draft").toBool() || o.value("prerelease").toBool()) {
        return std::nullopt;
    }
    Release r{*v, tag.startsWith('v') ? tag.mid(1) : tag, "github", {}, {}};
    for (const QJsonValue& a : o.value("assets").toArray()) {
        const QString name = a.toObject().value("name").toString();
        const QUrl url(a.toObject().value("browser_download_url").toString());
        if (name == zip_name(r.text)) {
            r.zip = url;
        } else if (name == zip_name(r.text) + ".sha256") {
            r.sha256 = url;
        }
    }
    if (r.zip.isEmpty()) {
        return std::nullopt; // sin el .zip de Windows no hay nada que poner
    }
    return r;
}

QString Updater::updateScript() {
    // En ASCII: Windows PowerShell 5.1 lee un .ps1 sin BOM como ANSI. Solo cmdlets, para que valga también en
    // ConstrainedLanguage (con Smart App Control).
    return QStringLiteral(R"PS(param([int]$AppPid, [string]$Zip, [string]$Install, [string]$Log)
# GenClipPro: pone la version nueva en el sitio de la instalada (lo lanza Ajustes > Version > Actualizar).
$ErrorActionPreference = 'Stop'
function Write-Log([string]$Text) { Add-Content -LiteralPath $Log -Value ((Get-Date -Format 's') + ' ' + $Text) }
function Move-Retry([string]$From, [string]$To) {
    for ($i = 1; $i -le 10; $i++) {
        try { Move-Item -LiteralPath $From -Destination $To; return } catch { if ($i -eq 10) { throw }; Start-Sleep -Seconds 1 }
    }
}
$exe = Join-Path $Install 'genclip-studio.exe'
$new = $Install + '.nueva'
$old = $Install + '.vieja'
try {
    Write-Log ('Esperando a que se cierre GenClipPro (' + $AppPid + ')')
    Wait-Process -Id $AppPid -Timeout 120 -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $new) { Remove-Item -LiteralPath $new -Recurse -Force }
    New-Item -ItemType Directory -Path $new | Out-Null
    Write-Log ('Descomprimiendo ' + $Zip)
    & (Join-Path $env:SystemRoot 'System32\tar.exe') -xf $Zip -C $new
    if ($LASTEXITCODE -ne 0) { throw ('tar termino con ' + $LASTEXITCODE) }
    if (-not (Test-Path -LiteralPath (Join-Path $new 'genclip-studio.exe'))) { throw 'el .zip no trae genclip-studio.exe' }
    if (Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old -Recurse -Force }
    Move-Retry $Install $old
    Move-Retry $new $Install
    Write-Log 'Lista: se abre la nueva'
    Start-Process -FilePath $exe -WorkingDirectory $Install
    Remove-Item -LiteralPath $old -Recurse -Force -ErrorAction SilentlyContinue
} catch {
    Write-Log ('No se pudo actualizar: ' + $_)
    if (-not (Test-Path -LiteralPath $Install) -and (Test-Path -LiteralPath $old)) { Move-Item -LiteralPath $old -Destination $Install }
    if (Test-Path -LiteralPath $new) { Remove-Item -LiteralPath $new -Recurse -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $exe) { Start-Process -FilePath $exe -WorkingDirectory $Install }
}
)PS");
}

Updater::Updater(QObject* parent)
    : QObject(parent), app_dir_(to_path(QCoreApplication::applicationDirPath())),
      local_dir_(to_path(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)) / "releases") {
    QString repo = kRepo;
    QFile marker(qpath(app_dir_ / kMarker));
    if (marker.open(QIODevice::ReadOnly)) {
        if (const QString r = QJsonDocument::fromJson(marker.readAll()).object().value("repo").toString(); !r.isEmpty()) {
            repo = r;
        }
    }
    api_url_ = QUrl(QString("https://api.github.com/repos/%1/releases/latest").arg(repo));
}

Updater::~Updater() {
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
    }
}

void Updater::setAppDir(const fs::path& dir) { app_dir_ = dir; }
void Updater::setLocalReleases(const fs::path& dir) { local_dir_ = dir; }
void Updater::setApiUrl(const QUrl& url) { api_url_ = url; }

QString Updater::version() const { return current_text(); }

bool Updater::installed() const {
    std::error_code ec;
    return fs::is_regular_file(app_dir_ / kMarker, ec);
}

void Updater::set(const QString& status, const QString& message, const QString& error) {
    status_ = status;
    message_ = message;
    error_ = error;
    emit changed();
}

void Updater::check() {
    if (status_ == "checking" || status_ == "downloading" || status_ == "installing") {
        return;
    }
    found_ = newestLocal(local_dir_, parseVersion(current_text()).value_or(Version{}));
    set("checking", QStringLiteral("Buscando una versión nueva…"));
    QNetworkRequest req = request(api_url_, 15000);
    req.setRawHeader("Accept", "application/vnd.github+json");
    QNetworkReply* reply = net_.get(req);
    reply_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { checked(reply); });
}

void Updater::checked(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply != reply_) {
        return;
    }
    QString net_error;
    if (reply->error() == QNetworkReply::NoError) {
        const auto gh = fromGithub(reply->readAll(), parseVersion(current_text()).value_or(Version{}));
        if (gh && (!found_ || gh->version > found_->version)) {
            found_ = gh;
        }
    } else if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 404) { // 404: aún sin releases
        net_error = reply->errorString();
    }
    if (found_) {
        set("available", QString("Hay una versión nueva: la %1, %2.")
                             .arg(found_->text, found_->source == "local" ? "de una release de este PC" : "de GitHub"));
    } else if (!net_error.isEmpty()) {
        set("error", {}, "No se pudo mirar en GitHub: " + net_error);
    } else {
        set("uptodate", QStringLiteral("Ya tienes la última versión."));
    }
}

void Updater::update() {
    if (status_ != "available" || !found_) {
        return;
    }
    if (!installed()) {
        set("error", {}, QStringLiteral("Esta es la de desarrollo: se pone al día compilando."));
        return;
    }
    // Sin su .sha256 no se instala: es lo que dice que el .zip llegó entero y es el de la release.
    if (found_->sha256.isEmpty()) {
        set("error", {}, QString("La %1 no trae su .sha256: no se instala.").arg(found_->text));
        return;
    }
    if (found_->source != "local") {
        download();
        return;
    }
    QByteArray sha;
    if (QFile f(found_->sha256.toLocalFile()); f.open(QIODevice::ReadOnly)) {
        sha = sha_from_text(f.readAll());
    }
    install(found_->zip.toLocalFile(), sha);
}

void Updater::download() {
    const QString zip = QDir(update_dir()).filePath(zip_name(found_->text));
    progress_ = 0;
    set("downloading", QString("Bajando la %1 de GitHub…").arg(found_->text));
    const auto fetch_zip = [this, zip] {
        file_ = std::make_unique<QFile>(zip);
        if (!file_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file_.reset();
            set("error", {}, "No se puede escribir " + QDir::toNativeSeparators(zip));
            return;
        }
        QNetworkReply* reply = net_.get(request(found_->zip, 60000));
        reply_ = reply;
        connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
            if (file_) {
                file_->write(reply->readAll());
            }
        });
        connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
            if (total > 0) {
                progress_ = static_cast<double>(got) / static_cast<double>(total);
                emit changed();
            }
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, zip] {
            reply->deleteLater();
            if (file_) {
                file_->write(reply->readAll());
                file_->close();
                file_.reset();
            }
            if (reply->error() != QNetworkReply::NoError) {
                set("error", {}, "No se pudo bajar la versión nueva: " + reply->errorString());
                return;
            }
            install(zip, sha_);
        });
    };
    // Primero el .sha256 (update() ya sabe que lo hay) y luego el .zip.
    sha_.clear();
    QNetworkReply* reply = net_.get(request(found_->sha256, 30000));
    reply_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, fetch_zip] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            set("error", {}, "No se pudo bajar el .sha256 de la versión nueva: " + reply->errorString());
            return;
        }
        sha_ = sha_from_text(reply->readAll());
        fetch_zip();
    });
}

void Updater::install(const QString& zip, const QByteArray& expected_sha) {
    if (expected_sha.isEmpty()) {
        set("error", {}, QString("El .sha256 de la %1 no es válido: no se instala.").arg(found_->text));
        return;
    }
    if (file_sha256(zip) != expected_sha) {
        set("error", {}, QString("El .zip de la %1 no coincide con su .sha256: no se instala.").arg(found_->text));
        return;
    }
    const QString dir = update_dir();
    const QString script = QDir(dir).filePath("actualizar.ps1");
    QFile f(script);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(updateScript().toLatin1()) < 0) {
        set("error", {}, "No se pudo escribir " + QDir::toNativeSeparators(script));
        return;
    }
    f.close();
    const QStringList args{"-NoProfile",
                           "-ExecutionPolicy",
                           "Bypass",
                           "-WindowStyle",
                           "Hidden",
                           "-File",
                           QDir::toNativeSeparators(script),
                           "-AppPid",
                           QString::number(QCoreApplication::applicationPid()),
                           "-Zip",
                           QDir::toNativeSeparators(zip),
                           "-Install",
                           QDir::toNativeSeparators(qpath(app_dir_)),
                           "-Log",
                           QDir::toNativeSeparators(QDir(dir).filePath("actualizar.log"))};
    set("installing", QString("Se cierra GenClipPro para poner la %1 y se vuelve a abrir.").arg(found_->text));
    emit readyToRestart(script, args);
    if (dry_run_) {
        return;
    }
    // Desde la carpeta temporal: si su carpeta actual fuera la de la app, Windows no dejaría moverla.
    if (!QProcess::startDetached("powershell.exe", args, dir)) {
        set("error", {}, QStringLiteral("No se pudo lanzar PowerShell para actualizar."));
        return;
    }
    QCoreApplication::quit();
}

} // namespace gc::studio
