#include "cv_draw.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

// Port de modules/imgproc/src/drawing.cpp de OpenCV 5.0.0 (Apache 2.0), solo lo que usa
// paint_edit: un canal de 8 bits, color 255 y LINE_AA. Mismo orden de operaciones, mismos tipos.

namespace gc::core::detail {

namespace {

constexpr int kShift = 16;                                // XY_SHIFT
constexpr std::int64_t kOne = std::int64_t{1} << kShift; // XY_ONE

// cvRound: al entero más cercano, los empates al par (como _mm_cvtsd_si32).
std::int64_t cv_round(double v) { return std::llrint(v); }

constexpr std::array<int, 32> kSlopeCorr = {181, 181, 181, 182, 182, 183, 184, 185, 187, 188, 190,
                                            192, 194, 196, 198, 201, 203, 206, 209, 211, 214, 218,
                                            221, 224, 227, 231, 235, 238, 242, 246, 250, 254};

constexpr std::array<int, 64> kFilter = {
    168, 177, 185, 194, 202, 210, 218, 224, 231, 236, 241, 246, 249, 252, 254, 254,
    254, 254, 252, 249, 246, 241, 236, 231, 224, 218, 210, 202, 194, 185, 177, 168,
    158, 149, 140, 131, 122, 114, 105, 97,  89,  82,  75,  68,  62,  56,  50,  45,
    40,  36,  32,  28,  25,  22,  19,  16,  14,  12,  11,  9,   8,   7,   5,   5};

constexpr std::array<float, 451> kSin = {
#include "cv_sin_table.inc"
};

// clipLine(Size2l, Point2l&, Point2l&)
bool clip_line(std::int64_t width, std::int64_t height, CvPoint& p1, CvPoint& p2) {
    const std::int64_t right = width - 1;
    const std::int64_t bottom = height - 1;
    if (width <= 0 || height <= 0) {
        return false;
    }
    std::int64_t& x1 = p1.x;
    std::int64_t& y1 = p1.y;
    std::int64_t& x2 = p2.x;
    std::int64_t& y2 = p2.y;
    const auto code = [&](std::int64_t x, std::int64_t y) {
        return static_cast<int>(x < 0) + (static_cast<int>(x > right) * 2) + (static_cast<int>(y < 0) * 4) +
               (static_cast<int>(y > bottom) * 8);
    };
    const auto along = [](std::int64_t a, std::int64_t b, std::int64_t num, std::int64_t den) {
        return static_cast<std::int64_t>(static_cast<double>(a - b) * static_cast<double>(num) /
                                         static_cast<double>(den));
    };
    int c1 = code(x1, y1);
    int c2 = code(x2, y2);
    if ((c1 & c2) == 0 && (c1 | c2) != 0) {
        std::int64_t a = 0;
        if ((c1 & 12) != 0) {
            a = c1 < 8 ? 0 : bottom;
            x1 += along(a, y1, x2 - x1, y2 - y1);
            y1 = a;
            c1 = static_cast<int>(x1 < 0) + (static_cast<int>(x1 > right) * 2);
        }
        if ((c2 & 12) != 0) {
            a = c2 < 8 ? 0 : bottom;
            x2 += along(a, y2, x2 - x1, y2 - y1);
            y2 = a;
            c2 = static_cast<int>(x2 < 0) + (static_cast<int>(x2 > right) * 2);
        }
        if ((c1 & c2) == 0 && (c1 | c2) != 0) {
            if (c1 != 0) {
                a = c1 == 1 ? 0 : right;
                y1 += along(a, x1, y2 - y1, x2 - x1);
                x1 = a;
                c1 = 0;
            }
            if (c2 != 0) {
                a = c2 == 1 ? 0 : right;
                y2 += along(a, x2, y2 - y1, x2 - x1);
                x2 = a;
                c2 = 0;
            }
        }
    }
    return (c1 | c2) == 0;
}

// ICV_PUT_POINT de un canal: mezclar el blanco con `a` (en 1/256), dos veces, como OpenCV.
void put(CvGray& img, int x, int y, int a) {
    std::uint8_t& t =
        img.px[(static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width)) + static_cast<std::size_t>(x)];
    int c = t;
    c += (((255 - c) * a) + 127) >> 8;
    c += (((255 - c) * a) + 127) >> 8;
    t = static_cast<std::uint8_t>(c);
}

void line_aa(CvGray& img, CvPoint pt1, CvPoint pt2) {
    int scount = 0;
    std::array<int, 9> ep{};
    if (!clip_line(std::int64_t{img.width} << kShift, std::int64_t{img.height} << kShift, pt1, pt2)) {
        return;
    }
    std::int64_t dx = pt2.x - pt1.x;
    std::int64_t dy = pt2.y - pt1.y;
    std::int64_t j = dx < 0 ? -1 : 0;
    const std::int64_t ax = (dx ^ j) - j;
    std::int64_t i = dy < 0 ? -1 : 0;
    const std::int64_t ay = (dy ^ i) - i;
    std::int64_t x_step = 0;
    std::int64_t y_step = 0;
    int ecount = 0;
    int slope = 0;
    if (ax > ay) {
        dy = (dy ^ j) - j;
        pt1.x ^= pt2.x & j;
        pt2.x ^= pt1.x & j;
        pt1.x ^= pt2.x & j;
        pt1.y ^= pt2.y & j;
        pt2.y ^= pt1.y & j;
        pt1.y ^= pt2.y & j;
        x_step = kOne;
        y_step = static_cast<std::int64_t>(static_cast<std::uint64_t>(dy) << kShift) / (ax | 1);
        pt2.x += kOne;
        ecount = static_cast<int>((pt2.x >> kShift) - (pt1.x >> kShift));
        j = -(pt1.x & (kOne - 1));
        pt1.y += ((y_step * j) >> kShift) + (kOne >> 1);
        slope = static_cast<int>((y_step >> (kShift - 5)) & 0x3f);
        slope ^= (y_step < 0 ? 0x3f : 0);
        i = (pt1.x >> (kShift - 7)) & 0x78;
        j = (pt2.x >> (kShift - 7)) & 0x78;
    } else {
        dx = (dx ^ i) - i;
        pt1.x ^= pt2.x & i;
        pt2.x ^= pt1.x & i;
        pt1.x ^= pt2.x & i;
        pt1.y ^= pt2.y & i;
        pt2.y ^= pt1.y & i;
        pt1.y ^= pt2.y & i;
        x_step = static_cast<std::int64_t>(static_cast<std::uint64_t>(dx) << kShift) / (ay | 1);
        y_step = kOne;
        pt2.y += kOne;
        ecount = static_cast<int>((pt2.y >> kShift) - (pt1.y >> kShift));
        j = -(pt1.y & (kOne - 1));
        pt1.x += ((x_step * j) >> kShift) + (kOne >> 1);
        slope = static_cast<int>((x_step >> (kShift - 5)) & 0x3f);
        slope ^= (x_step < 0 ? 0x3f : 0);
        i = (pt1.y >> (kShift - 7)) & 0x78;
        j = (pt2.y >> (kShift - 7)) & 0x78;
    }
    slope = (slope & 0x20) != 0 ? 0x100 : kSlopeCorr[static_cast<std::size_t>(slope)];
    {
        const int t0 = slope << 7;
        const int t1 = ((0x78 - static_cast<int>(i)) | 4) * slope;
        const int t2 = (static_cast<int>(j) | 4) * slope;
        ep[0] = 0;
        ep[8] = slope;
        ep[1] = ep[3] = static_cast<int>(((((j - i) & 0x78) | 4) * slope >> 8) & 0x1ff);
        ep[2] = (t1 >> 8) & 0x1ff;
        ep[4] = static_cast<int>(((((j - i) + 0x80) | 4) * slope >> 8) & 0x1ff);
        ep[5] = ((t1 + t0) >> 8) & 0x1ff;
        ep[6] = (t2 >> 8) & 0x1ff;
        ep[7] = ((t2 + t0) >> 8) & 0x1ff;
    }
    const auto corr = [&](int s, int e) {
        const int si = (static_cast<int>(s >= 2) + 1) & (s | 2);
        const int ei = (static_cast<int>(e >= 2) + 1) & (e | 2);
        const int k = (si * 3) + ei;
        return ep[static_cast<std::size_t>(k)];
    };
    const auto filter = [](int k) { return kFilter[static_cast<std::size_t>(k)]; };
    if (ax > ay) {
        int x = static_cast<int>(pt1.x >> kShift);
        for (; ecount >= 0; x++, pt1.y += y_step, scount++, ecount--) {
            if (static_cast<unsigned>(x) >= static_cast<unsigned>(img.width)) {
                continue;
            }
            const int y = static_cast<int>((pt1.y >> kShift) - 1);
            const int ep_corr = corr(scount, ecount);
            const int dist = static_cast<int>((pt1.y >> (kShift - 5)) & 31);
            if (static_cast<unsigned>(y) < static_cast<unsigned>(img.height)) {
                put(img, x, y, (ep_corr * filter(dist + 32) >> 8) & 0xff);
            }
            if (static_cast<unsigned>(y + 1) < static_cast<unsigned>(img.height)) {
                put(img, x, y + 1, (ep_corr * filter(dist) >> 8) & 0xff);
            }
            if (static_cast<unsigned>(y + 2) < static_cast<unsigned>(img.height)) {
                put(img, x, y + 2, (ep_corr * filter(63 - dist) >> 8) & 0xff);
            }
        }
    } else {
        int y = static_cast<int>(pt1.y >> kShift);
        for (; ecount >= 0; y++, pt1.x += x_step, scount++, ecount--) {
            if (static_cast<unsigned>(y) >= static_cast<unsigned>(img.height)) {
                continue;
            }
            const int x = static_cast<int>((pt1.x >> kShift) - 1);
            const int ep_corr = corr(scount, ecount);
            const int dist = static_cast<int>((pt1.x >> (kShift - 5)) & 31);
            if (static_cast<unsigned>(x) < static_cast<unsigned>(img.width)) {
                put(img, x, y, (ep_corr * filter(dist + 32) >> 8) & 0xff);
            }
            if (static_cast<unsigned>(x + 1) < static_cast<unsigned>(img.width)) {
                put(img, x + 1, y, (ep_corr * filter(dist) >> 8) & 0xff);
            }
            if (static_cast<unsigned>(x + 2) < static_cast<unsigned>(img.width)) {
                put(img, x + 2, y, (ep_corr * filter(63 - dist) >> 8) & 0xff);
            }
        }
    }
}

// FillConvexPoly con LINE_AA: el borde con line_aa y el interior en blanco.
void fill_convex_poly_aa(CvGray& img, std::span<const CvPoint> v, int shift) {
    struct Edge {
        int idx = 0;
        int di = 0;
        std::int64_t x = 0;
        std::int64_t dx = 0;
        int ye = 0;
    };
    std::array<Edge, 2> edge{};
    const int npts = static_cast<int>(v.size());
    const auto at = [&](int k) { return v[static_cast<std::size_t>(k)]; };
    const int delta = 1 << shift >> 1;
    int imin = 0;
    int edges = npts;
    constexpr std::int64_t kDelta1 = kOne - 1; // LINE_AA: el interior, sin el píxel del borde
    constexpr std::int64_t kDelta2 = 0;
    CvPoint p0 = at(npts - 1);
    p0.x <<= kShift - shift;
    p0.y <<= kShift - shift;
    std::int64_t xmin = v[0].x;
    std::int64_t xmax = v[0].x;
    std::int64_t ymin = v[0].y;
    std::int64_t ymax = v[0].y;
    for (int i = 0; i < npts; i++) {
        CvPoint p = at(i);
        if (p.y < ymin) {
            ymin = p.y;
            imin = i;
        }
        ymax = std::max(ymax, p.y);
        xmax = std::max(xmax, p.x);
        xmin = std::min(xmin, p.x);
        p.x <<= kShift - shift;
        p.y <<= kShift - shift;
        line_aa(img, p0, p);
        p0 = p;
    }
    xmin = (xmin + delta) >> shift;
    xmax = (xmax + delta) >> shift;
    ymin = (ymin + delta) >> shift;
    ymax = (ymax + delta) >> shift;
    if (npts < 3 || static_cast<int>(xmax) < 0 || static_cast<int>(ymax) < 0 || static_cast<int>(xmin) >= img.width ||
        static_cast<int>(ymin) >= img.height) {
        return;
    }
    ymax = std::min<std::int64_t>(ymax, img.height - 1);
    edge[0].idx = edge[1].idx = imin;
    int y = static_cast<int>(ymin);
    edge[0].ye = edge[1].ye = y;
    edge[0].di = 1;
    edge[1].di = npts - 1;
    edge[0].x = edge[1].x = -kOne;
    edge[0].dx = edge[1].dx = 0;
    do {
        if (y < static_cast<int>(ymax) || y == static_cast<int>(ymin)) {
            for (auto& e : edge) {
                if (y < e.ye) {
                    continue;
                }
                int idx0 = e.idx;
                const int di = e.di;
                int idx = idx0 + di;
                if (idx >= npts) {
                    idx -= npts;
                }
                for (; edges-- > 0;) {
                    const int ty = static_cast<int>((at(idx).y + delta) >> shift);
                    if (ty > y) {
                        std::int64_t xs = at(idx0).x;
                        std::int64_t xe = at(idx).x;
                        if (shift != kShift) {
                            xs <<= kShift - shift;
                            xe <<= kShift - shift;
                        }
                        const std::int64_t rows = static_cast<std::int64_t>(ty) - y;
                        e.ye = ty;
                        e.dx = (((xe - xs) * 2) + rows) / (2 * rows);
                        e.x = xs;
                        e.idx = idx;
                        break;
                    }
                    idx0 = idx;
                    idx += di;
                    if (idx >= npts) {
                        idx -= npts;
                    }
                }
            }
        }
        if (edges < 0) {
            break;
        }
        if (y >= 0) {
            const bool swap = edge[0].x > edge[1].x;
            const Edge& left = edge[swap ? 1 : 0];
            const Edge& right = edge[swap ? 0 : 1];
            int xx1 = static_cast<int>((left.x + kDelta1) >> kShift);
            int xx2 = static_cast<int>((right.x + kDelta2) >> kShift);
            if (xx2 >= 0 && xx1 < img.width) {
                xx1 = std::max(xx1, 0);
                xx2 = std::min(xx2, img.width - 1);
                if (xx2 >= xx1) { // ICV_HLINE (con xx2 = xx1 − 1, OpenCV no pinta nada)
                    std::memset(img.px.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width)) +
                                    static_cast<std::size_t>(xx1),
                                255, static_cast<std::size_t>(xx2) - static_cast<std::size_t>(xx1) + 1);
                }
            }
        }
        edge[0].x += edge[0].dx;
        edge[1].x += edge[1].dx;
    } while (++y <= static_cast<int>(ymax));
}

// EllipseEx de un círculo relleno (ángulo 0, de 0 a 360°, thickness −1), centro y radio en 16.16, con
// el polígono de ellipse2Poly.
void ellipse_filled_aa(CvGray& img, CvPoint center, std::int64_t radius) {
    radius = std::abs(radius);
    int delta = static_cast<int>((radius + (kOne >> 1)) >> kShift);
    delta = delta < 3 ? 90 : delta < 10 ? 30 : delta < 15 ? 18 : 5;
    const auto alpha = static_cast<double>(kSin[450]); // sincos(0)
    const auto beta = static_cast<double>(kSin[0]);
    const auto cx = static_cast<double>(center.x);
    const auto cy = static_cast<double>(center.y);
    const auto r = static_cast<double>(radius);
    std::vector<CvPoint> v;
    CvPoint prev{-1, -1}; // Point2l(0xFFFFFFFFFFFFFFFF, …): −1, que ningún punto puede valer aquí
    for (int i = 0; i < 360 + delta; i += delta) {
        const int angle = std::min(i, 360);
        const double x = r * static_cast<double>(kSin[static_cast<std::size_t>(450 - angle)]);
        const double y = r * static_cast<double>(kSin[static_cast<std::size_t>(angle)]);
        const double px = cx + (x * alpha) - (y * beta);
        const double py = cy + (x * beta) + (y * alpha);
        CvPoint pt;
        pt.x = cv_round(px / static_cast<double>(kOne)) << kShift;
        pt.y = cv_round(py / static_cast<double>(kOne)) << kShift;
        pt.x += cv_round(px - static_cast<double>(pt.x));
        pt.y += cv_round(py - static_cast<double>(pt.y));
        if (pt.x != prev.x || pt.y != prev.y) {
            v.push_back(pt);
            prev = pt;
        }
    }
    if (v.size() <= 1) {
        v.assign(2, center);
    }
    fill_convex_poly_aa(img, v, kShift);
}

// ThickLine con LINE_AA y shift 0.
void thick_line_aa(CvGray& img, CvPoint p0, CvPoint p1, int thickness, int flags) {
    const auto inside = [&](const CvPoint& p) { return p.x >= 0 && p.y >= 0 && p.x < img.width && p.y < img.height; };
    if (thickness > 1 && (!inside(p0) || !inside(p1))) {
        const std::int64_t margin = thickness;
        p0 = {p0.x + margin, p0.y + margin};
        p1 = {p1.x + margin, p1.y + margin};
        clip_line(img.width + (2 * margin), img.height + (2 * margin), p0, p1);
        p0 = {p0.x - margin, p0.y - margin};
        p1 = {p1.x - margin, p1.y - margin};
    }
    p0 = {p0.x << kShift, p0.y << kShift};
    p1 = {p1.x << kShift, p1.y << kShift};
    if (thickness <= 1) {
        line_aa(img, p0, p1);
        return;
    }
    constexpr double kInvOne = 1.0 / static_cast<double>(kOne);
    const double dx = static_cast<double>(p0.x - p1.x) * kInvOne;
    const double dy = static_cast<double>(p1.y - p0.y) * kInvOne;
    double r = (dx * dx) + (dy * dy);
    const int odd = thickness & 1;
    const std::int64_t thick = static_cast<std::int64_t>(thickness) << (kShift - 1);
    if (std::fabs(r) > std::numeric_limits<double>::epsilon()) { // DBL_EPSILON
        r = (static_cast<double>(thick) + (odd * static_cast<double>(kOne) * 0.5)) / std::sqrt(r);
        const CvPoint dp{cv_round(dy * r), cv_round(dx * r)};
        const std::array<CvPoint, 4> pt = {CvPoint{p0.x + dp.x, p0.y + dp.y}, CvPoint{p0.x - dp.x, p0.y - dp.y},
                                           CvPoint{p1.x - dp.x, p1.y - dp.y}, CvPoint{p1.x + dp.x, p1.y + dp.y}};
        fill_convex_poly_aa(img, pt, kShift);
    }
    for (int i = 0; i < 2; i++) {
        if ((flags & (i + 1)) != 0) {
            ellipse_filled_aa(img, p0, thick);
        }
        p0 = p1;
    }
}

} // namespace

void cv_polylines_aa(CvGray& img, const std::vector<CvPoint>& pts, int thickness) {
    int flags = 3; // abierto: 2 + !is_closed (un extremo redondo en cada punta)
    for (std::size_t i = 1; i < pts.size(); i++) {
        thick_line_aa(img, pts[i - 1], pts[i], thickness, flags);
        flags = 2;
    }
}

void cv_circle_filled_aa(CvGray& img, CvPoint center, int radius) {
    ellipse_filled_aa(img, CvPoint{center.x << kShift, center.y << kShift}, static_cast<std::int64_t>(radius) << kShift);
}

} // namespace gc::core::detail
