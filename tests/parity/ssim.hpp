#pragma once

// Medidas de imagen de los tests de paridad del render y del export.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gc::test {

// SSIM de la luma BT.709 (Wang et al. 2004): ventana gaussiana de 11×11 con σ = 1,5,
// C1 = (0,01·255)² y C2 = (0,03·255)², la media del mapa en las ventanas enteras dentro de la imagen
// que no tocan ningún píxel de `skip` (en el render, la banda del borde, que tampoco cuenta en el
// PSNR: un píxel de más del export bajaría todas las ventanas de alrededor). 1 si no queda ninguna.
// `a` y `b`: RGBA de w × h, sin relleno. Coincide con ssim_index.m de Wang (filter2 'valid') hasta
// 5·10⁻⁷ en las fixtures de render.
inline double ssim_luma(int w, int h, std::span<const std::uint8_t> a, std::span<const std::uint8_t> b,
                        const std::vector<bool>* skip = nullptr) {
    constexpr int kR = 5; // 11×11
    constexpr double kSigma = 1.5;
    constexpr double kC1 = (0.01 * 255) * (0.01 * 255);
    constexpr double kC2 = (0.03 * 255) * (0.03 * 255);
    if (w <= 2 * kR || h <= 2 * kR) {
        return 1.0;
    }
    constexpr int kTaps = (2 * kR) + 1;
    std::array<double, kTaps> g{};
    double gs = 0.0;
    for (int i = 0; i < kTaps; ++i) {
        const int d = i - kR;
        g[static_cast<std::size_t>(i)] = std::exp(-(d * d) / (2.0 * kSigma * kSigma));
        gs += g[static_cast<std::size_t>(i)];
    }
    for (double& v : g) {
        v /= gs;
    }
    const auto n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    // Las cinco señales (x, y, x², y², xy), filtradas por filas y luego por columnas.
    std::array<std::vector<double>, 5> sig;
    for (auto& s : sig) {
        s.resize(n);
    }
    for (std::size_t k = 0; k < n; ++k) {
        const double x = (0.2126 * a[4 * k]) + (0.7152 * a[(4 * k) + 1]) + (0.0722 * a[(4 * k) + 2]);
        const double y = (0.2126 * b[4 * k]) + (0.7152 * b[(4 * k) + 1]) + (0.0722 * b[(4 * k) + 2]);
        sig[0][k] = x;
        sig[1][k] = y;
        sig[2][k] = x * x;
        sig[3][k] = y * y;
        sig[4][k] = x * y;
    }
    const auto at = [w](int x, int y) {
        return (static_cast<std::size_t>(y) * static_cast<std::size_t>(w)) + static_cast<std::size_t>(x);
    };
    for (auto& s : sig) {
        std::vector<double> rows(n, 0.0);
        for (int y = 0; y < h; ++y) {
            for (int x = kR; x < w - kR; ++x) {
                double acc = 0.0;
                for (int i = 0; i < kTaps; ++i) {
                    acc += g[static_cast<std::size_t>(i)] * s[at(x + i - kR, y)];
                }
                rows[at(x, y)] = acc;
            }
        }
        for (int y = kR; y < h - kR; ++y) {
            for (int x = kR; x < w - kR; ++x) {
                double acc = 0.0;
                for (int i = 0; i < kTaps; ++i) {
                    acc += g[static_cast<std::size_t>(i)] * rows[at(x, y + i - kR)];
                }
                s[at(x, y)] = acc;
            }
        }
    }
    // Cuántos píxeles de `skip` hay en cada ventana: con la suma acumulada en 2D.
    std::vector<int> integral(static_cast<std::size_t>(w + 1) * static_cast<std::size_t>(h + 1), 0);
    const auto stride = static_cast<std::size_t>(w) + 1;
    const auto ii = [stride](int x, int y) {
        return (static_cast<std::size_t>(y) * stride) + static_cast<std::size_t>(x);
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            integral[ii(x + 1, y + 1)] = static_cast<int>(skip != nullptr && (*skip)[at(x, y)]) + integral[ii(x, y + 1)] +
                                         integral[ii(x + 1, y)] - integral[ii(x, y)];
        }
    }
    double sum = 0.0;
    std::size_t windows = 0;
    for (int y = kR; y < h - kR; ++y) {
        for (int x = kR; x < w - kR; ++x) {
            const int touched = integral[ii(x + kR + 1, y + kR + 1)] - integral[ii(x - kR, y + kR + 1)] -
                                integral[ii(x + kR + 1, y - kR)] + integral[ii(x - kR, y - kR)];
            if (touched != 0) {
                continue;
            }
            const std::size_t k = at(x, y);
            const double mx = sig[0][k];
            const double my = sig[1][k];
            const double vx = sig[2][k] - (mx * mx);
            const double vy = sig[3][k] - (my * my);
            const double cxy = sig[4][k] - (mx * my);
            sum += (((2 * mx * my) + kC1) * ((2 * cxy) + kC2)) / (((mx * mx) + (my * my) + kC1) * (vx + vy + kC2));
            ++windows;
        }
    }
    return windows == 0 ? 1.0 : sum / static_cast<double>(windows);
}

} // namespace gc::test
