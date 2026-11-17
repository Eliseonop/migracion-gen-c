#include "player.hpp"

#include <QSettings>
#include <algorithm>
#include <cmath>
#include <utility>

#include "editor_model.hpp"

#ifdef GC_STUDIO_PREVIEW
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

#include "gc/export/audio.hpp"
#include "gc/export/project.hpp"
#include "gc/media/audio_out.hpp"
#endif

namespace gc::studio {

#ifdef GC_STUDIO_PREVIEW

struct Player::Audio {
    explicit Audio(Player* o) : owner(o) {
        thread = std::thread([this] { run(); });
    }
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;
    Audio(Audio&&) = delete;
    Audio& operator=(Audio&&) = delete;
    ~Audio() {
        {
            const std::scoped_lock lock(mutex);
            stop = true;
        }
        wake.notify_all();
        thread.join();
    }

    Player* owner;
    // Del hilo de la interfaz.
    std::optional<media::AudioOut> out; // se abre al reproducir por primera vez
    QString out_error;                  // si no se pudo abrir: no se vuelve a intentar
    core::TimelineSnapshot timeline;    // la de ahora
    core::TimelineSnapshot sounding;    // la de la mezcla que suena
    core::TimelineSnapshot asked;       // la última que se ha pedido mezclar
    // Lo pendiente para el hilo de la mezcla, bajo `mutex`.
    std::mutex mutex;
    std::condition_variable wake;
    bool stop = false;
    bool reload = false;
    std::filesystem::path root;
    std::string project_id;
    core::TimelineSnapshot want;
    std::thread thread; // el último: arranca con todo lo demás ya hecho

    void post(core::TimelineSnapshot tl, std::shared_ptr<const std::vector<float>> pcm, QString error) const {
        Player* o = owner;
        QMetaObject::invokeMethod(
            o, [o, tl = std::move(tl), pcm = std::move(pcm), error = std::move(error)] { o->mixed(tl, pcm, error); },
            Qt::QueuedConnection);
    }

    void run() {
        std::optional<exporter::LoadedProject> project;
        QString project_error;
        for (;;) {
            bool reload_now = false;
            std::filesystem::path new_root;
            std::string new_id;
            core::TimelineSnapshot tl;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return stop || reload || want; });
                if (stop) {
                    return;
                }
                reload_now = std::exchange(reload, false);
                new_root = std::move(root);
                new_id = std::move(project_id);
                tl = std::exchange(want, nullptr);
            }
            if (reload_now) {
                auto loaded = exporter::load_project(new_root, new_id);
                project_error = loaded ? QString() : QString::fromStdString(loaded.error().message);
                project = loaded ? std::optional(std::move(*loaded)) : std::nullopt;
            }
            if (!tl) {
                continue;
            }
            if (!project) {
                post(tl, nullptr, project_error);
                continue;
            }
            exporter::LoadedProject p = *project;
            p.timeline = *tl;
            auto pcm = exporter::mix_timeline_audio(p);
            if (!pcm) {
                post(tl, nullptr, QString::fromStdString(pcm.error().message));
            } else {
                post(tl, std::make_shared<const std::vector<float>>(std::move(*pcm)), {});
            }
        }
    }
};

#else // sin gc_export: sin mezcla ni salida de audio, el reloj de pared

struct Player::Audio {};

#endif

Player::Player(EditorModel& editor) : editor_(editor) {
#ifdef GC_STUDIO_PREVIEW
    audio_ = std::make_unique<Audio>(this);
#endif
    // parsePreviewVolume: lo que no es un número, 1; y entre 0 y 1.
    bool ok = false;
    const double saved = QSettings("GenClipPro", "studio").value("preview/volume", 1.0).toDouble(&ok);
    volume_ = ok && std::isfinite(saved) ? std::clamp(saved, 0.0, 1.0) : 1.0;
    timer_.setInterval(16); // 60 Hz
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &Player::tick);
}

Player::~Player() = default;

void Player::setVolume(double volume) {
    const double v = std::isfinite(volume) ? std::clamp(volume, 0.0, 1.0) : 1.0;
    if (v == volume_) {
        return;
    }
    volume_ = v;
    QSettings("GenClipPro", "studio").setValue("preview/volume", v);
#ifdef GC_STUDIO_PREVIEW
    if (audio_->out) {
        audio_->out->set_volume(v);
    }
#endif
    emit volumeChanged();
}

void Player::toggle() {
    if (playing_) {
        pause();
    } else {
        play();
    }
}

void Player::play() {
    const double duration = editor_.duration();
    if (playing_ || duration <= 0) {
        return;
    }
    if (editor_.playhead() >= duration) {
        moving_ = true;
        editor_.setPlayhead(0);
        moving_ = false;
    }
    const double from = editor_.playhead();
#ifdef GC_STUDIO_PREVIEW
    Audio& a = *audio_;
    if (!a.out && a.out_error.isEmpty()) {
        auto out = media::AudioOut::open();
        if (out) {
            a.out.emplace(std::move(*out));
            a.out->set_pcm(nullptr);
            a.out->set_volume(volume_);
        } else {
            a.out_error = "Sin salida de audio: " + QString::fromStdString(out.error().message);
        }
    }
    if (a.out) {
        if (auto started = a.out->play_from(from); !started) {
            a.out.reset(); // el reloj de pared sigue
            a.out_error = QString::fromStdString(started.error().message);
        }
    }
    if (!a.out_error.isEmpty()) {
        set_audio(false, a.out_error);
    }
#else
    set_audio(false, "Sin audio: compila con MSVC + vcpkg (gc_export)");
#endif
    wall_from_ = from;
    wall_start_ = std::chrono::steady_clock::now();
    playing_ = true;
    timer_.start();
    ask_mix();
    emit playingChanged();
}

void Player::pause() {
    if (!playing_) {
        return;
    }
#ifdef GC_STUDIO_PREVIEW
    if (audio_->out) {
        audio_->out->pause();
    }
#endif
    timer_.stop();
    playing_ = false;
    emit playingChanged();
}

double Player::clock() const {
#ifdef GC_STUDIO_PREVIEW
    if (audio_->out && audio_->out->playing()) {
        return audio_->out->position();
    }
#endif
    return wall_from_ + std::chrono::duration<double>(std::chrono::steady_clock::now() - wall_start_).count();
}

void Player::restart_clock(double seconds) {
    wall_from_ = seconds;
    wall_start_ = std::chrono::steady_clock::now();
#ifdef GC_STUDIO_PREVIEW
    if (audio_->out && audio_->out->playing()) {
        static_cast<void>(audio_->out->play_from(seconds)); // ya sonaba: solo salta
    }
#endif
}

void Player::seeked(double seconds) {
    if (playing_ && !moving_) {
        restart_clock(seconds);
    }
}

void Player::tick() {
    const double duration = editor_.duration();
    const double t = clock();
    moving_ = true;
    editor_.setPlayhead(std::min(t, duration));
    moving_ = false;
    if (t >= duration) {
        pause(); // al final, quieto en la duración
    }
}

void Player::set_audio(bool audible, const QString& error) {
    if (audible != audible_ || error != audio_error_) {
        audible_ = audible;
        audio_error_ = error;
        emit audioChanged();
    }
}

#ifdef GC_STUDIO_PREVIEW

void Player::open(std::filesystem::path root, std::string project_id) {
    {
        const std::scoped_lock lock(audio_->mutex);
        audio_->reload = true;
        audio_->root = std::move(root);
        audio_->project_id = std::move(project_id);
    }
    audio_->wake.notify_one();
}

void Player::close() {
    pause();
    Audio& a = *audio_;
    a.timeline = a.sounding = a.asked = nullptr;
    if (a.out) {
        a.out->set_pcm(nullptr);
    }
    {
        const std::scoped_lock lock(a.mutex);
        a.want = nullptr;
    }
    set_audio(false, a.out_error);
}

void Player::setTimeline(core::TimelineSnapshot timeline) {
    Audio& a = *audio_;
    if (timeline == a.timeline) {
        return;
    }
    a.timeline = std::move(timeline);
    if (a.timeline != a.sounding) {
        set_audio(false, a.out_error); // hasta que llegue la nueva suena la anterior
    }
    if (playing_) {
        ask_mix();
    }
}

void Player::ask_mix() {
    Audio& a = *audio_;
    if (!a.timeline || a.timeline == a.sounding || a.timeline == a.asked) {
        return;
    }
    a.asked = a.timeline;
    {
        const std::scoped_lock lock(a.mutex);
        a.want = a.timeline;
    }
    a.wake.notify_one();
}

void Player::mixed(const core::TimelineSnapshot& timeline, const std::shared_ptr<const std::vector<float>>& pcm,
                   const QString& error) {
    Audio& a = *audio_;
    if (timeline != a.timeline) {
        return; // ya hay otra: su mezcla está pedida o se pedirá al reproducir
    }
    if (!pcm) {
        set_audio(false, error);
        return;
    }
    a.sounding = timeline;
    if (a.out) {
        a.out->set_pcm(pcm); // sin moverse: entra en su sitio
    }
    set_audio(a.out.has_value(), a.out_error);
}

#else

void Player::open(std::filesystem::path /*root*/, std::string /*project_id*/) {}
void Player::close() { pause(); }
void Player::setTimeline(core::TimelineSnapshot /*timeline*/) {}
void Player::ask_mix() {}
void Player::mixed(const core::TimelineSnapshot& /*timeline*/, const std::shared_ptr<const std::vector<float>>& /*pcm*/,
                   const QString& /*error*/) {}

#endif

} // namespace gc::studio
