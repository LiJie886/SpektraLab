// Numeric gates for Vulkan scene latitude and contrast-mask kernels. The
// references below intentionally use double arithmetic, independent of GLSL.
#include "gpu/gpu.hpp"
#include "gpu_vulkan_read.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>
namespace {
using spk::gpu::Arg;
using spk::gpu::BufferRef;
using spk::gpu::Gpu;
bool close(Gpu *g, const BufferRef &b, const std::vector<double> &ref, const char *label,
           std::string &e, double at = 2e-5, double rt = 2e-5) {
    if (!g->flush(e))
        return false;
    std::vector<float> a;
    if (!read_test_buffer(g, b, a, e))
        return false;
    for (size_t i = 0; i < ref.size(); ++i) {
        double tol = at + rt * std::abs(ref[i]);
        bool bad = std::isnan(ref[i]) ? !std::isnan(a[i])
                   : std::isinf(ref[i])
                       ? double(a[i]) != ref[i]
                       : (!std::isfinite(a[i]) || std::abs(double(a[i]) - ref[i]) > tol);
        if (bad) {
            char s[240];
            std::snprintf(s, sizeof s, "%s[%zu] %.9g != %.17g (tol %.3g)", label, i, a[i], ref[i],
                          tol);
            e = s;
            return false;
        }
    }
    return true;
}
BufferRef up(Gpu *g, const void *p, size_t n, std::string &e) { return g->upload(p, n, e); }
double dep(double d, double h, double m) {
    if (!(d > 0))
        return 0;
    if (m == 2) {
        double r = std::sqrt(h * h + d * d);
        return -d * d * d / (r * (h + r));
    }
    if (m == 1)
        return -d * d / (h + d);
    return d * (h / std::pow(std::pow(h, m) + std::pow(d, m), 1 / m) - 1);
}
double lat(double r, double g, double b, const float *p, uint32_t i, uint32_t row0) {
    if (std::isnan(r) || std::isnan(g) || std::isnan(b))
        return 1;
    double cr = std::max(r, 0.), cg = std::max(g, 0.), cb = std::max(b, 0.), v;
    unsigned n = (unsigned)p[6];
    if (n == 1)
        v = p[7] * cr + p[8] * cg + p[9] * cb;
    else if (n == 2)
        v = std::max({cr, cg, cb});
    else {
        double s = cr * cr + cg * cg + cb * cb;
        v = s > 0 ? (cr * cr * cr + cg * cg * cg + cb * cb * cb) / s : 0;
    }
    if (!(v > 0) || !std::isfinite(v))
        return 1;
    double E =
        std::clamp(std::log2(std::max(v, double(p[10]) * std::exp2(-24.)) / p[10]), -24., 24.);
    double kh = p[0], hh = p[1], ks = p[2], hs = p[3];
    unsigned w = (unsigned)p[11], h = (unsigned)p[12];
    if (p[13] > 0) {
        double along = w >= h ? (double(i % w) + .5) / w : (double(i / w) + row0 + .5) / h;
        if (along >= p[13]) {
            kh = p[14];
            hh = p[15];
            ks = p[16];
            hs = p[17];
        }
    }
    double d = hh > 0 && E > kh   ? dep(E - kh, hh, p[4])
               : hs > 0 && E < ks ? -dep(ks - E, hs, p[4])
                                  : 0;
    if (d > 0)
        d = d * p[5] / std::sqrt(double(p[5]) * p[5] + d * d);
    return std::clamp(std::exp2(d), std::exp2(-30.), std::exp2(30.));
}
bool latitude(Gpu *g, std::string &e) {
    const uint32_t W = 7, H = 5, row0 = 2, n = W * 3;
    std::vector<float> rgb(n * 3);
    for (uint32_t i = 0; i < n; ++i) {
        float x = float(int(i % 17) - 5) / 8;
        rgb[3 * i] = x;
        rgb[3 * i + 1] = x * .7f;
        rgb[3 * i + 2] = x * 1.3f;
    }
    rgb[0] = 0;
    rgb[1] = -1;
    rgb[2] = 0;
    rgb[3] = std::numeric_limits<float>::quiet_NaN();
    rgb[4] = 1;
    rgb[5] = 2;
    rgb[6] = std::numeric_limits<float>::infinity();
    rgb[9] = -std::numeric_limits<float>::infinity();
    for (unsigned norm = 0; norm < 3; ++norm)
        for (float m : {1.f, 2.f, 1.5f}) {
            float p[19] = {.8f, .7f, -.9f, .8f,  m,    2.f, float(norm), .2126f, .7152f,     .0722f,
                           1.f, W,   H,    .48f, -.3f, .9f, .4f,         .6f,    float(row0)};
            std::vector<double> ref(n * 3);
            for (uint32_t i = 0; i < n; ++i) {
                double k = lat(rgb[3 * i], rgb[3 * i + 1], rgb[3 * i + 2], p, i, row0);
                for (int c = 0; c < 3; ++c)
                    ref[3 * i + c] = double(rgb[3 * i + c]) * k;
            }
            auto a = up(g, rgb.data(), rgb.size() * 4, e), b = up(g, p, sizeof p, e),
                 c = up(g, &n, 4, e), o = up(g, ref.data(), ref.size() * 8, e);
            if (!a || !b || !c || !o)
                return false;
            if (!g->dispatch("spk_scene_latitude",
                             {Arg::buf(a), Arg::buf(b), Arg::buf(c), Arg::buf(o)}, n, e) ||
                !close(g, o, ref, "latitude", e, 2e-5, 2e-5))
                return false;
        }
    return true;
}
double mx(const double A[3], const float *p) {
    double s = 0;
    for (int k = 0; k < 3; ++k)
        s += std::max(std::log2(std::max(A[k] * p[3 + k], 0.)), -20.);
    return s / 3.;
}
double mg(double d, double h) { return d * h / std::sqrt(h * h + d * d); }
double md(double b, const float *p) {
    double d = 0;
    if (p[11] != 0 && b < p[9]) {
        double q = p[9] - b;
        d = q - mg(q, p[10]);
    } else if (p[14] != 0 && b > p[12]) {
        double q = b - p[12];
        d = mg(q, p[13]) - q;
    }
    return d * p[15] / std::sqrt(p[15] * p[15] + d * d);
}
bool mask(Gpu *g, std::string &e) {
    const uint32_t W = 5, H = 3, gw = 3, gh = 2, n = W * H, nl = 2;
    std::vector<float> cmy(n * 3), chd(nl * 3), base = {.05f, -.12f}, ixs(nl * 3);
    float p[16] = {1.2f, .8f, 1.6f, 1, 1, 1, 0, 0, 0, -.7f, .7f, 1, -1.4f, .9f, 1, 1.3f};
    for (uint32_t i = 0; i < n; ++i) {
        cmy[3 * i] = float(int(i % 7) - 2) * .17f;
        cmy[3 * i + 1] = float(int(i % 5) - 1) * .13f;
        cmy[3 * i + 2] = float(int(i % 9) - 3) * .11f;
    }
    float chv[] = {.2f, -.1f, .3f, -.17f, .25f, .08f}, iv[] = {.2f, .3f, .4f, .1f, .35f, .15f};
    std::copy(chv, chv + 6, chd.begin());
    std::copy(iv, iv + 6, ixs.begin());
    auto a = up(g, cmy.data(), cmy.size() * 4, e), b = up(g, chd.data(), chd.size() * 4, e),
         c = up(g, base.data(), base.size() * 4, e), d = up(g, ixs.data(), ixs.size() * 4, e),
         q = up(g, p, sizeof p, e);
    uint32_t rm[] = {W, H, gw, gh, nl};
    auto m = up(g, rm, sizeof rm, e);
    std::vector<float> ro(gw * gh);
    auto o = up(g, ro.data(), ro.size() * 4, e);
    if (!a || !b || !c || !d || !q || !m || !o)
        return false;
    auto pixelA = [&](uint32_t i, double A[3]) {
        A[0] = A[1] = A[2] = 0;
        for (uint32_t l = 0; l < nl; ++l) {
            double den = 0;
            for (int k = 0; k < 3; ++k)
                den += double(cmy[3 * i + k]) * chd[3 * l + k];
            den += base[l];
            double t = std::exp2(-den * 3.321928094887362);
            for (int k = 0; k < 3; ++k)
                A[k] += t * ixs[3 * l + k] * p[k];
        }
    };
    std::vector<double> rr(gw * gh);
    for (uint32_t cy = 0; cy < gh; ++cy)
        for (uint32_t cx = 0; cx < gw; ++cx) {
            uint32_t x0 = cx * W / gw, x1 = std::min(std::max((cx + 1) * W / gw, x0 + 1), W),
                     y0 = cy * H / gh, y1 = std::min(std::max((cy + 1) * H / gh, y0 + 1), H);
            double sum = 0;
            for (uint32_t y = y0; y < y1; ++y)
                for (uint32_t x = x0; x < x1; ++x) {
                    double A[3];
                    pixelA(y * W + x, A);
                    sum += mx(A, p);
                }
            rr[cy * gw + cx] = sum / ((x1 - x0) * (y1 - y0));
        }
    if (!g->dispatch("spk_mask_reduce",
                     {Arg::buf(a), Arg::buf(b), Arg::buf(c), Arg::buf(d), Arg::buf(q), Arg::buf(m),
                      Arg::buf(o)},
                     gw * gh, e) ||
        !close(g, o, rr, "mask_reduce", e, 3e-5, 3e-5))
        return false;
    // Nonuniform coefficients drive both interpolation and edge clamping. Run
    // both curve branches, then both disabled (identity), on the whole image and
    // a band beginning at frame row one; their pixels must agree.
    const uint32_t bc = gw * gh * 2;
    std::vector<float> coef(bc);
    for (uint32_t j = 0; j < gw * gh; ++j) {
        coef[2 * j] = float(int(j % 5) - 2) * .22f;
        coef[2 * j + 1] = float(int(j % 7) - 3) * .19f;
    }
    auto cf = up(g, coef.data(), coef.size() * 4, e);
    if (!cf)
        return false;
    for (int mode = 0; mode < 4; ++mode) {
        float ep[16];
        std::copy(p, p + 16, ep);
        if (mode == 1) {
            ep[11] = ep[14] = 0;
        }
        if (mode == 2)
            ep[11] = 0;
        if (mode == 3)
            ep[14] = 0;
        auto qp = up(g, ep, sizeof ep, e);
        if (!qp)
            return false;
        auto run = [&](uint32_t row0, uint32_t rows, std::vector<float> &vals) {
            std::vector<float> sub(cmy.begin() + size_t(row0) * W * 3,
                                   cmy.begin() + size_t(row0 + rows) * W * 3),
                zeros(size_t(rows) * W * 3);
            auto in = up(g, sub.data(), sub.size() * 4, e);
            uint32_t meta[] = {rows * W, nl, W, H, row0, gw, gh};
            auto mb = up(g, meta, sizeof meta, e), out = up(g, zeros.data(), zeros.size() * 4, e);
            if (!in || !mb || !out ||
                !g->dispatch("spk_mask_epilogue",
                             {Arg::buf(in), Arg::buf(b), Arg::buf(c), Arg::buf(d), Arg::buf(qp),
                              Arg::buf(cf), Arg::buf(mb), Arg::buf(out)},
                             rows * W, e))
                return false;
            std::vector<double> expected(size_t(rows) * W * 3);
            for (uint32_t y = 0; y < rows; ++y)
                for (uint32_t x = 0; x < W; ++x) {
                    double A[3];
                    pixelA((row0 + y) * W + x, A);
                    double u = std::clamp((x + .5) * gw / W - .5, 0., double(gw - 1)),
                           v = std::clamp((row0 + y + .5) * gh / H - .5, 0., double(gh - 1));
                    uint32_t u0 = uint32_t(std::floor(u)), v0 = uint32_t(std::floor(v)),
                             u1 = std::min(u0 + 1, gw - 1), v1 = std::min(v0 + 1, gh - 1);
                    double tu = u - u0, tv = v - v0, aa = 0, bb = 0;
                    for (int yy = 0; yy < 2; ++yy)
                        for (int xx = 0; xx < 2; ++xx) {
                            uint32_t cell = (yy ? v1 : v0) * gw + (xx ? u1 : u0);
                            double weight = (yy ? tv : 1 - tv) * (xx ? tu : 1 - tu);
                            aa += weight * coef[2 * cell];
                            bb += weight * coef[2 * cell + 1];
                        }
                    double k = std::exp2(md(aa * mx(A, ep) + bb, ep));
                    for (int z = 0; z < 3; ++z)
                        expected[(size_t(y) * W + x) * 3 + z] =
                            std::log10(std::max(k * A[z] + ep[6 + z], 0.) + 1e-10);
                }
            if (!close(g, out, expected, "mask_epilogue", e, 5e-5, 5e-5))
                return false;
            std::vector<float> all;
            if (!read_test_buffer(g, out, all, e))
                return false;
            vals = std::move(all);
            return true;
        };
        std::vector<float> whole, band;
        if (!run(0, H, whole) || !run(1, H - 1, band))
            return false;
        for (size_t i = 0; i < size_t(H - 1) * W * 3; ++i)
            if (std::abs(band[i] - whole[size_t(W) * 3 + i]) > 5e-5f) {
                char msg[180];
                std::snprintf(msg, sizeof msg, "mask band mode=%d i=%zu band=%.9g full=%.9g", mode,
                              i, band[i], whole[size_t(W) * 3 + i]);
                e = msg;
                return false;
            }
    }
    return true;
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: spk_vulkan_tone <resources>\n");
        return 2;
    }
    std::string e;
    std::unique_ptr<Gpu> g(Gpu::create_vulkan(std::string(argv[1]) + "/vulkan", e));
    if (!g) {
        std::fprintf(stderr, "Vulkan: %s\n", e.c_str());
        return 1;
    }
    if (!latitude(g.get(), e) || !mask(g.get(), e)) {
        std::fprintf(stderr, "tone: %s\n", e.c_str());
        return 1;
    }
    std::puts("tone kernels: float64 references passed");
    return 0;
}
