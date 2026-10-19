#include "gc/core/beat_detect.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>

#include "py.hpp"

namespace gc::core {

namespace {

using Complex = std::complex<double>;

constexpr double kFps = static_cast<double>(kBeatSampleRate) / kBeatHop;
constexpr int kBands = 48;
constexpr double kBpmMin = 60;
constexpr double kBpmMax = 200;
constexpr double kBpmPrior = 120;
constexpr double kTightness = 100;
constexpr double kOnsetLag = 0.065; // retardo del flujo espectral respecto al golpe

// FFT compleja radix-2 en el sitio (n potencia de 2); `inverse` sin dividir entre n.
void fft(std::vector<Complex>& a, bool inverse = false) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1U;
        for (; (j & bit) != 0; bit >>= 1U) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(a[i], a[j]);
        }
    }
    const double sign = inverse ? 1 : -1;
    std::vector<Complex> twiddle(n / 2);
    for (std::size_t k = 0; k < n / 2; ++k) {
        twiddle[k] = std::polar(1.0, sign * 2 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(n));
    }
    for (std::size_t len = 2; len <= n; len <<= 1U) {
        const std::size_t step = n / len;
        for (std::size_t i = 0; i < n; i += len) {
            for (std::size_t k = 0; k < len / 2; ++k) {
                const Complex u = a[i + k];
                const Complex v = a[i + k + len / 2] * twiddle[k * step];
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
}

// np.convolve(a, v, mode="same"): longitud max(M, N), centrada como numpy.
std::vector<double> convolve_same(std::span<const double> a, std::span<const double> v) {
    if (v.size() > a.size()) {
        std::swap(a, v);
    }
    const std::size_t off = (v.size() - 1) / 2;
    std::vector<double> out(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        const std::size_t k = i + off; // índice en la convolución completa
        double sum = 0;
        for (std::size_t j = 0; j < v.size(); ++j) {
            if (k >= j && k - j < a.size()) {
                sum += a[k - j] * v[j];
            }
        }
        out[i] = sum;
    }
    return out;
}

// _band_matrix: cada banda (espaciadas por octavas, 30 Hz – 11 kHz) promedia sus bins.
std::vector<std::vector<std::pair<std::size_t, float>>> band_bins() {
    const std::size_t bins = kBeatFft / 2 + 1;
    const double val = 1.0 / (kBeatFft * (1.0 / kBeatSampleRate)); // rfftfreq
    std::vector<double> edges(kBands + 1);
    const double lo = std::log10(30.0);
    const double step = (std::log10(kBeatSampleRate / 2.0) - lo) / kBands;
    for (int i = 0; i <= kBands; ++i) {
        edges[static_cast<std::size_t>(i)] = std::pow(10.0, i * step + lo); // geomspace
    }
    edges.front() = 30.0;
    edges.back() = kBeatSampleRate / 2.0;
    std::vector<std::vector<std::pair<std::size_t, float>>> out(kBands);
    for (std::size_t b = 0; b < kBands; ++b) {
        std::vector<std::size_t> sel;
        for (std::size_t k = 0; k < bins; ++k) {
            const double f = static_cast<double>(k) * val;
            if (f >= edges[b] && f < edges[b + 1]) {
                sel.push_back(k);
            }
        }
        if (sel.empty()) { // banda más estrecha que un bin: el más cercano a su centro
            const double center = std::sqrt(edges[b] * edges[b + 1]);
            double best = std::numeric_limits<double>::infinity();
            for (std::size_t k = 0; k < bins; ++k) {
                best = std::min(best, std::abs(static_cast<double>(k) * val - center));
            }
            for (std::size_t k = 0; k < bins; ++k) {
                if (std::abs(static_cast<double>(k) * val - center) == best) {
                    sel.push_back(k);
                }
            }
        }
        for (const std::size_t k : sel) {
            out[b].emplace_back(k, static_cast<float>(1.0 / static_cast<double>(sel.size())));
        }
    }
    return out;
}

double mean(std::span<const double> v) {
    return v.empty() ? 0 : std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

double median(std::vector<double> v) {
    std::ranges::sort(v);
    const std::size_t n = v.size();
    return n % 2 == 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

} // namespace

std::vector<double> onset_envelope(std::span<const float> mono) {
    if (mono.size() < static_cast<std::size_t>(kBeatFft)) {
        return {};
    }
    const std::size_t n = 1 + (mono.size() - kBeatFft) / kBeatHop;
    std::vector<float> window(kBeatFft);
    for (std::size_t i = 0; i < window.size(); ++i) { // np.hanning: 0.5 + 0.5·cos(π·n/(M−1)), n = 1−M, 3−M…
        const double at = 1.0 - kBeatFft + 2.0 * static_cast<double>(i);
        window[i] = static_cast<float>(0.5 + 0.5 * std::cos(std::numbers::pi * at / (kBeatFft - 1)));
    }
    const auto bands = band_bins();
    std::vector<float> flux(n, 0.0F);
    std::vector<float> prev;
    std::vector<Complex> frame(kBeatFft);
    std::vector<float> spec(kBeatFft / 2 + 1);
    for (std::size_t f = 0; f < n; ++f) {
        for (std::size_t i = 0; i < frame.size(); ++i) {
            frame[i] = static_cast<double>(mono[f * kBeatHop + i] * window[i]);
        }
        fft(frame);
        for (std::size_t k = 0; k < spec.size(); ++k) {
            spec[k] = static_cast<float>(std::abs(frame[k]));
        }
        std::vector<float> logs(kBands);
        for (std::size_t b = 0; b < kBands; ++b) {
            double sum = 0;
            for (const auto& [k, w] : bands[b]) {
                sum += static_cast<double>(spec[k]) * static_cast<double>(w);
            }
            logs[b] = std::log1p(100.0F * static_cast<float>(sum));
        }
        if (!prev.empty()) {
            double rise = 0;
            for (std::size_t b = 0; b < kBands; ++b) {
                rise += static_cast<double>(std::max(0.0F, logs[b] - prev[b]));
            }
            flux[f] = static_cast<float>(rise / kBands);
        }
        prev = std::move(logs);
    }
    // Sin la tendencia lenta (media móvil de ~0,5 s) y normalizada.
    const std::size_t k = std::max<std::size_t>(1, static_cast<std::size_t>(kFps * 0.5));
    const std::vector<double> flux64(flux.begin(), flux.end());
    const std::vector<double> box(k, 1.0 / static_cast<double>(k));
    const std::vector<double> trend = convolve_same(flux64, box);
    std::vector<double> env(trend.size());
    for (std::size_t i = 0; i < env.size(); ++i) {
        env[i] = std::max(0.0, flux64[flux64.size() == 1 ? 0 : i] - trend[i]); // un solo fotograma se difunde
    }
    const double peak = env.empty() ? 0 : *std::ranges::max_element(env);
    if (peak > 0) {
        for (double& e : env) {
            e /= peak;
        }
    }
    return env;
}

double estimate_bpm(std::span<const double> envelope) {
    const std::size_t n = envelope.size();
    const double avg = mean(envelope);
    std::size_t size = 1;
    while (size < 2 * n) {
        size <<= 1U;
    }
    std::vector<Complex> spec(size);
    for (std::size_t i = 0; i < n; ++i) {
        spec[i] = envelope[i] - avg;
    }
    fft(spec);
    for (Complex& c : spec) {
        c = std::norm(c);
    }
    fft(spec, true);
    std::vector<double> ac(n);
    for (std::size_t i = 0; i < n; ++i) {
        ac[i] = spec[i].real() / static_cast<double>(size);
    }
    const auto lo = static_cast<std::ptrdiff_t>(std::floor(kFps * 60.0 / kBpmMax));
    const auto hi = std::min(static_cast<std::ptrdiff_t>(n) - 1,
                             static_cast<std::ptrdiff_t>(std::ceil(kFps * 60.0 / kBpmMin)));
    if (hi <= lo + 1) {
        return kBpmPrior;
    }
    std::ptrdiff_t best = lo;
    double top = -std::numeric_limits<double>::infinity();
    for (std::ptrdiff_t lag = lo; lag < hi; ++lag) {
        const double bpm = 60.0 * kFps / std::max(static_cast<double>(lag), 1e-9);
        const double prior = std::exp(-0.5 * std::pow(std::log2(bpm / kBpmPrior), 2));
        const double score = ac[static_cast<std::size_t>(lag)] * prior;
        if (score > top) {
            top = score;
            best = lag;
        }
    }
    if (lo < best && best < hi - 1) { // afinar con una parábola por el pico
        const auto at = [&](std::ptrdiff_t i) { return ac[static_cast<std::size_t>(i)]; };
        const double a = at(best - 1);
        const double b = at(best);
        const double c = at(best + 1);
        const double den = a - 2 * b + c;
        const double off = den != 0 ? 0.5 * (a - c) / den : 0.0;
        return 60.0 * kFps / (static_cast<double>(best) + off);
    }
    return 60.0 * kFps / static_cast<double>(best);
}

Result<std::vector<std::int64_t>> track_beats(std::span<const double> envelope, double bpm) {
    const double period = kFps * 60.0 / bpm;
    const double avg = mean(envelope);
    double var = 0;
    for (const double e : envelope) {
        var += (e - avg) * (e - avg);
    }
    const double std = envelope.empty() ? 0 : std::sqrt(var / static_cast<double>(envelope.size()));
    std::vector<double> norm(envelope.begin(), envelope.end());
    if (std > 0) {
        for (double& e : norm) {
            e /= std;
        }
    }
    const auto reach = static_cast<std::int64_t>(period);
    std::vector<double> kernel;
    for (std::int64_t w = -reach; w <= reach; ++w) {
        kernel.push_back(std::exp(-0.5 * std::pow(static_cast<double>(w) * 32.0 / period, 2)));
    }
    const std::vector<double> local = convolve_same(norm, kernel);
    const auto n = static_cast<std::int64_t>(local.size());
    if (n == 0) {
        return std::vector<std::int64_t>{};
    }
    std::vector<std::int64_t> window;
    for (auto w = -static_cast<std::int64_t>(std::nearbyint(2 * period));
         w <= -static_cast<std::int64_t>(std::nearbyint(period / 2)); ++w) {
        window.push_back(w);
    }
    std::vector<double> txwt;
    for (const std::int64_t w : window) {
        txwt.push_back(-kTightness * std::pow(std::log(static_cast<double>(-w) / period), 2));
    }
    const double local_max = *std::ranges::max_element(local);
    std::vector<double> cum(static_cast<std::size_t>(n));
    std::vector<std::int64_t> back(static_cast<std::size_t>(n), -1);
    bool first = true;
    for (std::int64_t i = 0; i < n; ++i) {
        const auto ui = static_cast<std::size_t>(i);
        std::optional<std::pair<double, std::int64_t>> best; // primer máximo, como argmax
        for (std::size_t w = 0; w < window.size(); ++w) {
            const std::int64_t idx = i + window[w];
            if (idx >= 0) {
                const double cand = cum[static_cast<std::size_t>(idx)] + txwt[w];
                if (!best || cand > best->first) {
                    best = std::pair{cand, idx};
                }
            }
        }
        if (!best) {
            cum[ui] = local[ui];
            continue;
        }
        if (first && local[ui] < 0.01 * local_max) {
            cum[ui] = local[ui];
            continue;
        }
        first = false;
        cum[ui] = local[ui] + best->first;
        back[ui] = best->second;
    }
    std::vector<std::size_t> maxes; // máximos locales de la puntuación
    for (std::size_t i = 1; i + 1 < cum.size(); ++i) {
        if (cum[i] > cum[i - 1] && cum[i] >= cum[i + 1]) {
            maxes.push_back(i);
        }
    }
    if (maxes.empty()) {
        return std::vector<std::int64_t>{};
    }
    std::vector<double> peaks;
    for (const std::size_t m : maxes) {
        peaks.push_back(cum[m]);
    }
    const double thr = 0.5 * median(peaks);
    std::int64_t tail = -1;
    for (const std::size_t m : maxes) {
        if (cum[m] >= thr) {
            tail = static_cast<std::int64_t>(m);
        }
    }
    if (tail < 0) { // ningún máximo llega al umbral: Python falla (IndexError)
        return fail(ErrorCode::InvalidArgument, "No se encontraron beats.");
    }
    std::vector<std::int64_t> beats;
    for (std::int64_t i = tail; i >= 0; i = back[static_cast<std::size_t>(i)]) {
        beats.push_back(i);
    }
    std::ranges::reverse(beats);
    // Sin los beats flojos del principio y del final (silencio antes o después de la música).
    double power = 0;
    for (const double l : local) {
        power += l * l;
    }
    const double floor = 0.5 * std::sqrt(power / static_cast<double>(local.size()));
    const auto strong = [&](std::int64_t f) { return local[static_cast<std::size_t>(f)] > floor; };
    const auto from = std::ranges::find_if(beats, strong);
    if (from == beats.end()) {
        return beats;
    }
    const auto to = std::ranges::find_if(beats.rbegin(), beats.rend(), strong).base();
    return std::vector<std::int64_t>(from, to);
}

Result<Beats> detect_beats(std::span<const float> mono) {
    if (mono.size() < static_cast<std::size_t>(kBeatFft)) {
        return fail(ErrorCode::InvalidArgument, "El clip no tiene audio suficiente para detectar beats.");
    }
    const std::size_t frames = 1 + (mono.size() - kBeatFft) / kBeatHop;
    if (frames > 1 && frames < static_cast<std::size_t>(kFps * 0.5)) { // numpy no puede restar la tendencia
        return fail(ErrorCode::InvalidArgument, "El clip no tiene audio suficiente para detectar beats.");
    }
    const std::vector<double> env = onset_envelope(mono);
    const double bpm = estimate_bpm(env);
    const auto frames_of_beats = track_beats(env, bpm);
    if (!frames_of_beats) {
        return std::unexpected(frames_of_beats.error());
    }
    Beats out;
    for (const std::int64_t f : *frames_of_beats) {
        out.times.push_back(detail::py_round(static_cast<double>(f * kBeatHop) / kBeatSampleRate + kOnsetLag, 3));
    }
    out.bpm = detail::py_round(bpm, 1);
    return out;
}

} // namespace gc::core
