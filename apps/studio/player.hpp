#pragma once

// El reproductor del editor (Espacio): reproduce desde el cabezal y el cabezal sigue al audio, con
// el reloj de la tarjeta (media::AudioOut: lo que ya ha sonado de la mezcla del export). La mezcla
// se prepara en un hilo al darle a reproducir, y otra vez si la timeline cambia mientras suena:
// entretanto el reloj corre igual (en silencio, o con la mezcla anterior) y el sonido nuevo entra
// en su sitio cuando llega. Sin salida de audio, o sin gc_export, un reloj de pared. El cabezal
// avanza a 60 Hz; el fotograma lo pide el editor a su vista previa, que salta al más nuevo.

#include <QObject>
#include <QString>
#include <QTimer>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "gc/core/session.hpp"

namespace gc::studio {

class EditorModel;

class Player : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    // Ya suena la mezcla de la timeline de ahora (tras un cambio, hasta que llega la nueva, no).
    Q_PROPERTY(bool audible READ audible NOTIFY audioChanged)
    // Por qué no suena (sin salida de audio, la mezcla falló…); vacío si suena o aún no se ha pedido.
    Q_PROPERTY(QString audioError READ audioError NOTIFY audioChanged)

public:
    explicit Player(EditorModel& editor);
    ~Player() override;
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    Player(Player&&) = delete;
    Player& operator=(Player&&) = delete;

    [[nodiscard]] bool playing() const { return playing_; }
    [[nodiscard]] bool audible() const { return audible_; }
    [[nodiscard]] QString audioError() const { return audio_error_; }

    Q_INVOKABLE void toggle();
    // Desde el cabezal; si está al final, desde el principio.
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();

    // Lo llama el editor, como a PreviewSource: el proyecto (para la mezcla) y su timeline.
    void open(std::filesystem::path root, std::string project_id);
    void close();
    void setTimeline(core::TimelineSnapshot timeline);
    // El cabezal se ha movido (no lo ha movido el reproductor): sonando, el reloj salta ahí.
    void seeked(double seconds);

signals:
    void playingChanged();
    void audioChanged();

private:
    struct Audio; // la salida de audio y el hilo de la mezcla (con gc_export)
    void tick();
    void ask_mix();
    [[nodiscard]] double clock() const;
    void restart_clock(double seconds);
    void mixed(const core::TimelineSnapshot& timeline, const std::shared_ptr<const std::vector<float>>& pcm,
               const QString& error);
    void set_audio(bool audible, const QString& error);

    EditorModel& editor_;
    QTimer timer_;
    bool playing_ = false;
    bool audible_ = false;
    bool moving_ = false; // el cabezal lo está moviendo el propio reproductor
    QString audio_error_;
    std::chrono::steady_clock::time_point wall_start_; // el reloj de pared: desde cuándo…
    double wall_from_ = 0;                             // …y desde qué instante
    std::unique_ptr<Audio> audio_;                     // el último: su hilo se para antes que lo demás
};

} // namespace gc::studio
