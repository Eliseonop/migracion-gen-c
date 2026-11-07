#include "gc/media/audio_out.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>
#include <utility>

#include <miniaudio.h>

namespace gc::media {

void PcmCursor::set_pcm(Pcm pcm) {
    const std::scoped_lock lock(mutex_);
    pcm_ = std::move(pcm);
}

double PcmCursor::seek(double seconds) {
    const std::scoped_lock lock(mutex_);
    frame_ = std::isfinite(seconds) && seconds > 0 ? std::llround(seconds * rate_) : 0;
    return static_cast<double>(frame_) / rate_;
}

void PcmCursor::fill(std::span<float> out) {
    const std::scoped_lock lock(mutex_);
    const auto frames = static_cast<std::int64_t>(out.size() / kExportChannels);
    std::size_t copied = 0;
    if (pcm_) {
        const auto have = static_cast<std::int64_t>(pcm_->size() / kExportChannels);
        if (frame_ < have) {
            const std::int64_t n = std::min(frames, have - frame_);
            const auto from = static_cast<std::ptrdiff_t>(frame_ * kExportChannels);
            copied = static_cast<std::size_t>(n * kExportChannels);
            std::copy_n(pcm_->begin() + from, copied, out.begin());
        }
    }
    std::fill(out.begin() + static_cast<std::ptrdiff_t>(copied), out.end(), 0.0F);
    frame_ += frames;
}

double PcmCursor::position() const {
    const std::scoped_lock lock(mutex_);
    return static_cast<double>(frame_) / rate_;
}

namespace {

// En el hilo de la tarjeta: lo siguiente que suena. La firma es la de ma_device_data_proc.
// NOLINTNEXTLINE(misc-const-correctness)
void data_callback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frames) {
    auto* cursor = static_cast<PcmCursor*>(device->pUserData);
    cursor->fill(std::span(static_cast<float*>(output), std::size_t{frames} * kExportChannels));
}

} // namespace

// En el montón y sin moverse: miniaudio guarda punteros a `device` y a `cursor`.
struct AudioOut::Impl {
    explicit Impl(int rate) : cursor(rate) {}
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    ~Impl() {
        if (has_device) {
            ma_device_uninit(&device); // para la tarjeta antes de soltar el cursor
        }
        if (has_context) {
            ma_context_uninit(&context);
        }
    }

    PcmCursor cursor;
    ma_context context{};
    // A cero hasta que ma_device_init lo rellena (lo pone entero a cero él también).
    ma_device device{}; // NOLINT(bugprone-invalid-enum-default-initialization)
    bool has_context = false;
    bool has_device = false;
    bool playing = false;
    double from = 0.0; // desde dónde suena (el último play_from)
};

AudioOut::AudioOut(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
AudioOut::AudioOut(AudioOut&&) noexcept = default;
AudioOut& AudioOut::operator=(AudioOut&&) noexcept = default;
AudioOut::~AudioOut() = default;

Result<AudioOut> AudioOut::open(bool null_device, int rate) {
    auto impl = std::make_unique<Impl>(rate);
    // NOLINTNEXTLINE(concurrency-mt-unsafe): solo se lee; nadie la cambia con la app en marcha
    const char* env = std::getenv("GC_AUDIO_DEVICE");
    const bool null = null_device || (env != nullptr && std::string_view(env) == "null");
    ma_backend null_backend = ma_backend_null;
    const ma_context_config context_config = ma_context_config_init();
    if (ma_context_init(null ? &null_backend : nullptr, null ? 1 : 0, &context_config, &impl->context) != MA_SUCCESS) {
        return fail(ErrorCode::Io, "no hay ninguna salida de audio");
    }
    impl->has_context = true;
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = kExportChannels;
    config.sampleRate = static_cast<ma_uint32>(rate);
    config.dataCallback = data_callback;
    config.pUserData = &impl->cursor;
    if (ma_device_init(&impl->context, &config, &impl->device) != MA_SUCCESS) {
        return fail(ErrorCode::Io, "no se pudo abrir la salida de audio");
    }
    impl->has_device = true;
    return AudioOut(std::move(impl));
}

void AudioOut::set_pcm(Pcm pcm) { impl_->cursor.set_pcm(std::move(pcm)); }

Result<void> AudioOut::play_from(double seconds) {
    impl_->from = impl_->cursor.seek(seconds);
    if (!impl_->playing) {
        if (ma_device_start(&impl_->device) != MA_SUCCESS) {
            return fail(ErrorCode::Io, "no se pudo empezar a reproducir el audio");
        }
        impl_->playing = true;
    }
    return {};
}

void AudioOut::pause() {
    if (impl_->playing) {
        ma_device_stop(&impl_->device);
        impl_->playing = false;
    }
}

bool AudioOut::playing() const noexcept { return impl_->playing; }

double AudioOut::position() const { return heard_position(filled(), impl_->from, latency()); }

double AudioOut::filled() const { return impl_->cursor.position(); }

double AudioOut::latency() const {
    const auto& playback = impl_->device.playback;
    if (!impl_->has_device || playback.internalSampleRate == 0) {
        return 0.0;
    }
    return static_cast<double>(playback.internalPeriodSizeInFrames) * playback.internalPeriods /
           playback.internalSampleRate;
}

std::string AudioOut::backend() const { return ma_get_backend_name(impl_->context.backend); }

} // namespace gc::media
