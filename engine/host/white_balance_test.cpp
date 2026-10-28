#include "white_balance.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

#include <libraw/libraw.h>

static void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

int main() {
    for (const auto [temperature, tint] : {std::pair{2500.0, -30.0}, std::pair{4200.0, 0.0},
                                           std::pair{6500.0, 25.0}, std::pair{12000.0, -10.0}}) {
        double x = 0, y = 0, t2 = 0, g2 = 0;
        spkhost::temperature_tint_to_xy(temperature, tint, x, y);
        spkhost::xy_to_temperature_tint(x, y, t2, g2);
        require(std::fabs(temperature - t2) < 450.0, "temperature roundtrip tolerance exceeded");
        require(std::fabs(tint - g2) < 12.0, "tint roundtrip tolerance exceeded");
    }
    std::vector<spkhost::BayerValue> samples;
    for (int c = 0; c < 3; ++c)
        for (int n = 0; n < 8; ++n)
            samples.push_back({c, 100.0 + c * 10.0, 0.0, 1000.0});
    std::array<double, 3> neutral{};
    std::string error;
    require(spkhost::bayer_neutral_from_samples(samples, neutral, error),
            "valid Bayer sample rejected");
    require(neutral[0] == 100 && neutral[1] == 110 && neutral[2] == 120,
            "Bayer channel means were incorrectly normalized");
    // A Bayer patch has two green sites; these are pooled into the same CFA channel.
    samples.clear();
    for (int c : {0, 1, 1, 2})
        for (int n = 0; n < 4; ++n)
            samples.push_back({c, double(80 + c * 10), 0, 1000});
    require(spkhost::bayer_neutral_from_samples(samples, neutral, error),
            "RGGB Bayer patch rejected");
    samples.push_back({0, 0, 0, 1000});
    samples.push_back({0, 0, 0, 1000});
    require(!spkhost::bayer_neutral_from_samples(samples, neutral, error),
            "black-contaminated Bayer patch accepted");
    samples.clear();
    for (int c = 0; c < 3; ++c)
        for (int n = 0; n < 4; ++n)
            samples.push_back({c, 999, 0, 1000});
    require(!spkhost::bayer_neutral_from_samples(samples, neutral, error),
            "saturated Bayer patch accepted");
    std::array<double, 9> identity{1, 0, 0, 0, 1, 0, 0, 0, 1};
    double cx = 0, cy = 0;
    require(spkhost::camera_neutral_to_xy(identity, {1, 1, 1}, cx, cy),
            "identity camera matrix rejected");
    require(!spkhost::camera_neutral_to_xy({}, {1, 1, 1}, cx, cy),
            "missing camera matrix accepted");
    size_t offset = 0;
    require(spkhost::raw_mosaic_offset(8, 6, 24, 5, 3, 1, 1, 4, 2, offset) && offset == 3 * 12 + 5,
            "odd active margins or padded RAW pitch mapped incorrectly");
    require(!spkhost::raw_mosaic_offset(8, 6, 15, 5, 3, 1, 1, 0, 0, offset),
            "odd RAW pitch accepted");
    require(!spkhost::raw_mosaic_offset(8, 6, 16, 5, 3, 1, 1, 5, 0, offset),
            "out-of-bounds active pixel accepted");

    LibRaw raw;
    raw.imgdata.idata.colors = 3;
    raw.imgdata.color.cam_xyz[0][0] = 1;
    raw.imgdata.color.cam_xyz[1][1] = 1;
    raw.imgdata.color.cam_xyz[2][2] = 1;
    std::array<float, 4> multipliers{};
    require(spkhost::multipliers_for(raw, 5000, 12, multipliers, error),
            "valid camera neutral did not produce multipliers");
    const std::array<double, 3> sensor_neutral{1.0 / multipliers[0], 1.0, 1.0 / multipliers[2]};
    require(spkhost::camera_neutral_to_xy(identity, sensor_neutral, cx, cy),
            "generated sensor neutral did not map back");
    double recovered_temperature = 0, recovered_tint = 0;
    spkhost::xy_to_temperature_tint(cx, cy, recovered_temperature, recovered_tint);
    require(std::fabs(recovered_temperature - 5000) < 30,
            "matrix and multiplier roundtrip temperature exceeded tolerance");
    require(std::fabs(recovered_tint - 12) < 1,
            "matrix and multiplier roundtrip tint exceeded tolerance");
    const uint32_t w = 5, h = 3;
    for (int orientation = 1; orientation <= 8; ++orientation) {
        const uint32_t dw = orientation >= 5 ? h : w;
        const uint32_t dh = orientation >= 5 ? w : h;
        for (uint32_t dy = 0; dy < dh; ++dy) {
            for (uint32_t dx = 0; dx < dw; ++dx) {
                uint32_t sx = 0, sy = 0;
                spkhost::inverse_oriented_pixel(w, h, orientation, dx, dy, sx, sy);
                require(sx < w && sy < h, "inverse EXIF orientation mapped outside source frame");
                uint32_t fx = sx, fy = sy;
                switch (orientation) {
                case 2:
                    fx = w - 1 - sx;
                    break;
                case 3:
                    fx = w - 1 - sx;
                    fy = h - 1 - sy;
                    break;
                case 4:
                    fy = h - 1 - sy;
                    break;
                case 5:
                    fx = sy;
                    fy = sx;
                    break;
                case 6:
                    fx = h - 1 - sy;
                    fy = sx;
                    break;
                case 7:
                    fx = h - 1 - sy;
                    fy = w - 1 - sx;
                    break;
                case 8:
                    fx = sy;
                    fy = w - 1 - sx;
                    break;
                }
                require(fx == dx && fy == dy, "inverse EXIF orientation did not roundtrip");
            }
        }
    }
}
