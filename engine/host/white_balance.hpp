// white_balance.hpp -- Temperature/Tint for RAW decode (PROTOCOL-REQUESTS R2).
//
// Kelvin and tint use the Adobe DNG SDK's definition (dng_temperature):
// Robertson's isotemperature lines in CIE 1960 uv, tint = -3000 x the signed
// distance from the Planckian locus along them (positive is magenta). That
// is the scale Lightroom, ACR and -- to the extent it is documented -- Core
// Image's CIRAWFilter use, so a value means roughly what it means on macOS.
//
// The camera side is LibRaw's own XYZ->camera matrix (`cam_xyz`, the
// DNG/Adobe ColorMatrix for D65), applied to the illuminant's XYZ: the
// neutral's camera response, whose reciprocal is the multiplier set. One
// matrix, not the DNG SDK's interpolation between two calibration
// illuminants, so far from D65 the mapping is an approximation; As Shot is
// computed the same way in reverse, which keeps "As Shot" -> custom with the
// same numbers a no-op (checked by host_smoke.py).
#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

class LibRaw;

namespace spkhost {

void temperature_tint_to_xy(double temperature_k, double tint, double& x, double& y);
void xy_to_temperature_tint(double x, double y, double& temperature_k, double& tint);
void inverse_oriented_pixel(uint32_t source_width, uint32_t source_height, int orientation,
                            uint32_t display_x, uint32_t display_y, uint32_t& source_x, uint32_t& source_y);
struct BayerValue { int channel; double value, black, white; };
bool bayer_neutral_from_samples(const std::vector<BayerValue>& samples, std::array<double, 3>& neutral,
                               std::string& error);
bool camera_neutral_to_xy(const std::array<double, 9>& xyz_to_camera, const std::array<double, 3>& neutral,
                          double& x, double& y);
bool camera_neutral_temperature_tint(const LibRaw& raw, const std::array<double, 3>& neutral,
                                     double& temperature_k, double& tint);
bool raw_mosaic_offset(uint32_t raw_width, uint32_t raw_height, uint32_t raw_pitch_bytes,
                       uint32_t active_width, uint32_t active_height, uint32_t left_margin,
                       uint32_t top_margin, uint32_t active_x, uint32_t active_y, size_t& offset);

// From an opened LibRaw: the as-shot illuminant, and multipliers for another.
bool as_shot_temperature_tint(const LibRaw& raw, double& temperature_k, double& tint);
bool multipliers_for(const LibRaw& raw, double temperature_k, double tint, std::array<float, 4>& mul,
                     std::string& error);

}  // namespace spkhost
