#include "white_balance.hpp"

#include <algorithm>
#include <cmath>

#include <libraw/libraw.h>

namespace spkhost {
namespace {

struct Ruvt { double r, u, v, t; };
// Robertson (1968), as tabulated in the DNG SDK: mired, u, v, isotherm slope.
constexpr Ruvt kTable[31] = {
    {0, 0.18006, 0.26352, -0.24341},   {10, 0.18066, 0.26589, -0.25479},
    {20, 0.18133, 0.26846, -0.26876},  {30, 0.18208, 0.27119, -0.28539},
    {40, 0.18293, 0.27407, -0.30470},  {50, 0.18388, 0.27709, -0.32675},
    {60, 0.18494, 0.28021, -0.35156},  {70, 0.18611, 0.28342, -0.37915},
    {80, 0.18740, 0.28668, -0.40955},  {90, 0.18880, 0.28997, -0.44278},
    {100, 0.19032, 0.29326, -0.47888}, {125, 0.19462, 0.30141, -0.58204},
    {150, 0.19962, 0.30921, -0.70471}, {175, 0.20525, 0.31647, -0.84901},
    {200, 0.21142, 0.32312, -1.0182},  {225, 0.21807, 0.32909, -1.2168},
    {250, 0.22511, 0.33439, -1.4512},  {275, 0.23247, 0.33904, -1.7298},
    {300, 0.24010, 0.34308, -2.0637},  {325, 0.24702, 0.34655, -2.4681},
    {350, 0.25591, 0.34951, -2.9641},  {375, 0.26400, 0.35200, -3.5814},
    {400, 0.27218, 0.35407, -4.3633},  {425, 0.28039, 0.35577, -5.3762},
    {450, 0.28863, 0.35714, -6.7262},  {475, 0.29685, 0.35823, -8.5955},
    {500, 0.30505, 0.35907, -11.324},  {525, 0.31320, 0.35968, -15.628},
    {550, 0.32129, 0.36011, -23.325},  {575, 0.32931, 0.36038, -40.770},
    {600, 0.33724, 0.36051, -116.45},
};
constexpr double kTintScale = -3000.0;

bool invert3(const double m[9], double out[9]) {
    const double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!std::isfinite(det) || std::fabs(det) < 1e-12) return false;
    const double r[9] = {(e * i - f * h), (c * h - b * i), (b * f - c * e),
                         (f * g - d * i), (a * i - c * g), (c * d - a * f),
                         (d * h - e * g), (b * g - a * h), (a * e - b * d)};
    for (int k = 0; k < 9; ++k) out[k] = r[k] / det;
    return true;
}

bool camera_matrix(const LibRaw& raw, double m[9]) {
    if (raw.imgdata.idata.colors != 3) return false;
    double sum = 0;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) {
            m[r * 3 + c] = raw.imgdata.color.cam_xyz[r][c];
            sum += std::fabs(m[r * 3 + c]);
        }
    if (sum > 0 && std::isfinite(sum)) return true;
    // A DNG can carry its own matrix without populating LibRaw's camera-table
    // cam_xyz field. Use its daylight profile, including calibration/analog
    // balance, on the same single-illuminant basis as Temperature/Tint above.
    const auto& color = raw.imgdata.color;
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& profile : color.dng_color) {
            if (pass == 0 && profile.illuminant != LIBRAW_WBI_D65) continue;
            // LibRaw 0.22 copies these public matrices from the chosen IFD
            // without copying parsedfields; inspect the actual values.
            double calibration_sum = 0;
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) calibration_sum += std::fabs(profile.calibration[r][c]);
            sum = 0;
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    double v = 0;
                    for (int k = 0; k < 3; ++k) {
                        const double calibration = calibration_sum > 0
                            ? profile.calibration[r][k] : double(r == k);
                        v += calibration * profile.colormatrix[k][c];
                    }
                    if (color.dng_levels.analogbalance[0] > 0 && color.dng_levels.analogbalance[1] > 0 && color.dng_levels.analogbalance[2] > 0)
                        v *= color.dng_levels.analogbalance[r];
                    m[3 * r + c] = v;
                    sum += std::fabs(v);
                }
            }
            if (sum > 0 && std::isfinite(sum)) return true;
        }
    }
    return false;
}

}  // namespace

void temperature_tint_to_xy(double temperature, double tint, double& x, double& y) {
    const double r = 1.0e6 / std::clamp(temperature, 1700.0, 50000.0);
    const double offset = tint * (1.0 / kTintScale);
    for (int i = 0; i < 30; ++i) {
        if (r < kTable[i + 1].r || i == 29) {
            const double f = (kTable[i + 1].r - r) / (kTable[i + 1].r - kTable[i].r);
            double u = kTable[i].u * f + kTable[i + 1].u * (1.0 - f);
            double v = kTable[i].v * f + kTable[i + 1].v * (1.0 - f);
            double uu1 = 1.0, vv1 = kTable[i].t, uu2 = 1.0, vv2 = kTable[i + 1].t;
            const double len1 = std::sqrt(1.0 + vv1 * vv1), len2 = std::sqrt(1.0 + vv2 * vv2);
            uu1 /= len1; vv1 /= len1; uu2 /= len2; vv2 /= len2;
            double uu3 = uu1 * f + uu2 * (1.0 - f), vv3 = vv1 * f + vv2 * (1.0 - f);
            const double len3 = std::sqrt(uu3 * uu3 + vv3 * vv3);
            uu3 /= len3; vv3 /= len3;
            u += uu3 * offset;
            v += vv3 * offset;
            x = 1.5 * u / (u - 4.0 * v + 2.0);
            y = v / (u - 4.0 * v + 2.0);
            return;
        }
    }
}

void xy_to_temperature_tint(double x, double y, double& temperature, double& tint) {
    const double u = 2.0 * x / (1.5 - x + 6.0 * y);
    const double v = 3.0 * y / (1.5 - x + 6.0 * y);
    double last_dt = 0, last_du = 0, last_dv = 0;
    temperature = 0;
    tint = 0;
    for (int i = 1; i <= 30; ++i) {
        double du = 1.0, dv = kTable[i].t;
        const double len = std::sqrt(1.0 + dv * dv);
        du /= len; dv /= len;
        double uu = u - kTable[i].u, vv = v - kTable[i].v;
        double dt = -uu * dv + vv * du;
        if (dt <= 0.0 || i == 30) {
            if (dt > 0.0) dt = 0.0;
            dt = -dt;
            const double f = i == 1 ? 0.0 : dt / (last_dt + dt);
            temperature = 1.0e6 / (kTable[i - 1].r * f + kTable[i].r * (1.0 - f));
            uu = u - (kTable[i - 1].u * f + kTable[i].u * (1.0 - f));
            vv = v - (kTable[i - 1].v * f + kTable[i].v * (1.0 - f));
            du = du * (1.0 - f) + last_du * f;
            dv = dv * (1.0 - f) + last_dv * f;
            const double l2 = std::sqrt(du * du + dv * dv);
            du /= l2; dv /= l2;
            tint = (uu * du + vv * dv) * kTintScale;
            return;
        }
        last_dt = dt; last_du = du; last_dv = dv;
    }
}

void inverse_oriented_pixel(uint32_t width, uint32_t height, int orientation,
                            uint32_t display_x, uint32_t display_y, uint32_t& source_x, uint32_t& source_y) {
    source_x = display_x;
    source_y = display_y;
    switch (orientation) {
        case 2: source_x = width - 1 - display_x; break;
        case 3: source_x = width - 1 - display_x; source_y = height - 1 - display_y; break;
        case 4: source_y = height - 1 - display_y; break;
        case 5: source_x = display_y; source_y = display_x; break;
        case 6: source_x = display_y; source_y = height - 1 - display_x; break;
        case 7: source_x = width - 1 - display_y; source_y = height - 1 - display_x; break;
        case 8: source_x = width - 1 - display_y; source_y = display_x; break;
        default: break;
    }
}

bool bayer_neutral_from_samples(const std::vector<BayerValue>& samples, std::array<double, 3>& neutral,
                               std::string& error) {
    double sums[3] = {}, good[3] = {}, rejected[3] = {};
    for (const auto& sample : samples) {
        if (sample.channel < 0 || sample.channel > 2 || !std::isfinite(sample.value) ||
            !std::isfinite(sample.black) || !std::isfinite(sample.white)) continue;
        const double span = sample.white - sample.black;
        const double value = sample.value - sample.black;
        if (!(span > 0) || value <= span * 0.01 || value >= span * 0.98) {
            rejected[sample.channel]++;
            continue;
        }
        sums[sample.channel] += value;
        good[sample.channel]++;
    }
    for (int c = 0; c < 3; ++c) {
        if (good[c] < 4) { error = "too few non-black, non-saturated samples for a Bayer channel"; return false; }
        if (rejected[c] / (good[c] + rejected[c]) > 0.20) {
            error = "too many black or saturated samples in a Bayer channel"; return false;
        }
        neutral[c] = sums[c] / good[c];
    }
    return true;
}

bool camera_neutral_to_xy(const std::array<double, 9>& matrix, const std::array<double, 3>& neutral,
                          double& x, double& y) {
    double inverse[9];
    if (!invert3(matrix.data(), inverse)) return false;
    const double xyz[3] = {
        inverse[0] * neutral[0] + inverse[1] * neutral[1] + inverse[2] * neutral[2],
        inverse[3] * neutral[0] + inverse[4] * neutral[1] + inverse[5] * neutral[2],
        inverse[6] * neutral[0] + inverse[7] * neutral[1] + inverse[8] * neutral[2],
    };
    const double sum=xyz[0]+xyz[1]+xyz[2];
    if (!(sum>0) || !std::isfinite(sum)) return false;
    x=xyz[0]/sum; y=xyz[1]/sum;
    return std::isfinite(x)&&std::isfinite(y);
}

bool camera_neutral_temperature_tint(const LibRaw& raw, const std::array<double, 3>& neutral,
                                     double& temperature, double& tint) {
    std::array<double, 9> matrix{};
    double x = 0, y = 0;
    if (!camera_matrix(raw, matrix.data()) || !camera_neutral_to_xy(matrix, neutral, x, y)) return false;
    xy_to_temperature_tint(x, y, temperature, tint);
    return std::isfinite(temperature) && std::isfinite(tint);
}

bool raw_mosaic_offset(uint32_t raw_width, uint32_t raw_height, uint32_t pitch_bytes,
                       uint32_t active_width, uint32_t active_height, uint32_t left_margin,
                       uint32_t top_margin, uint32_t active_x, uint32_t active_y, size_t& offset) {
    if (pitch_bytes < raw_width * sizeof(uint16_t) || pitch_bytes % sizeof(uint16_t) != 0 ||
        active_x >= active_width || active_y >= active_height ||
        left_margin + active_width > raw_width || top_margin + active_height > raw_height) return false;
    const uint64_t row = uint64_t(active_y + top_margin) * (pitch_bytes / sizeof(uint16_t));
    const uint64_t col = active_x + left_margin;
    const uint64_t index = row + col;
    if (index >= uint64_t(pitch_bytes / sizeof(uint16_t)) * raw_height) return false;
    offset = size_t(index);
    return true;
}

bool as_shot_temperature_tint(const LibRaw& raw, double& temperature, double& tint) {
    double m[9], inv[9];
    if (!camera_matrix(raw, m) || !invert3(m, inv)) return false;
    const float* mul = raw.imgdata.color.cam_mul;
    if (!(mul[0] > 0 && mul[1] > 0 && mul[2] > 0)) return false;
    const double neutral[3] = {1.0 / mul[0], 1.0 / mul[1], 1.0 / mul[2]};
    double xyz[3];
    for (int r = 0; r < 3; ++r) xyz[r] = inv[r * 3] * neutral[0] + inv[r * 3 + 1] * neutral[1] + inv[r * 3 + 2] * neutral[2];
    const double s = xyz[0] + xyz[1] + xyz[2];
    if (!(s > 0)) return false;
    xy_to_temperature_tint(xyz[0] / s, xyz[1] / s, temperature, tint);
    return std::isfinite(temperature) && std::isfinite(tint);
}

bool multipliers_for(const LibRaw& raw, double temperature, double tint, std::array<float, 4>& mul,
                     std::string& error) {
    double m[9];
    if (!camera_matrix(raw, m)) {
        error = "this camera has no three-colour XYZ matrix in LibRaw; white balance cannot be set";
        return false;
    }
    double x, y;
    temperature_tint_to_xy(temperature, tint, x, y);
    const double xyz[3] = {x / y, 1.0, (1.0 - x - y) / y};
    double n[3];
    for (int r = 0; r < 3; ++r) n[r] = m[r * 3] * xyz[0] + m[r * 3 + 1] * xyz[1] + m[r * 3 + 2] * xyz[2];
    if (!(n[0] > 0 && n[1] > 0 && n[2] > 0)) {
        error = "white balance outside what this camera can neutralise";
        return false;
    }
    mul = {float(n[1] / n[0]), 1.0f, float(n[1] / n[2]), 1.0f};
    return true;
}

}  // namespace spkhost
