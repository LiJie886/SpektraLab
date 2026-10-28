// Exercise Windows feature refusals through the exported DLL boundary.
#include "spektrafilm/spk_engine.h"

#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string params(spk_session* session) {
    char* json = nullptr;
    require(spk_get_params(session, &json) == SPK_OK && json, "get_params failed");
    std::string result(json);
    spk_string_free(json);
    return result;
}

void render(spk_engine* engine, spk_session* session) {
    spk_result result{};
    const spk_status status = spk_render(session, "full", &result);
    const bool valid = status == SPK_OK && result.rgba16 && result.texture &&
                       result.width == 32 && result.height == 16;
    const std::string error = spk_last_error(engine);
    spk_result_free(&result);
    require(valid, "default render failed after a refused request: " + error);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "usage: spk_windows_feature_boundaries resources_dir\n"; return 2; }
    try {
        std::unique_ptr<spk_engine, decltype(&spk_engine_destroy)> engine(
            spk_engine_create(argv[1], nullptr), spk_engine_destroy);
        require(bool(engine), std::string("engine creation failed: ") + spk_last_error(nullptr));
        const char* capabilities = spk_capabilities(engine.get());
        require(capabilities && std::string(capabilities).find("unsupported_features") != std::string::npos,
                "Windows capabilities must disclose unported effects");

        std::vector<float> pixels(32 * 16 * 3);
        for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = 0.025f + float(i % 31) * 0.025f;
        const spk_image input{pixels.data(), 32, 16, 3};
        std::unique_ptr<spk_session, decltype(&spk_session_release)> session(
            spk_open(engine.get(), &input, nullptr, nullptr), spk_session_release);
        require(bool(session), std::string("default open failed: ") + spk_last_error(engine.get()));
        render(engine.get(), session.get());
        const std::string before = params(session.get());

        struct Feature { const char* key; const char* label; };
        const std::array<Feature, 3> features{{
            {"digital_intermediate", "digital intermediate"},
            {"overscan_active", "overscan"},
            {"date_imprint_active", "date imprint"},
        }};
        for (const Feature& feature : features) {
            // Include a supported change to catch partial application before
            // the unsupported field is rejected.
            const std::string delta = std::string("{\"") + feature.key +
                                      "\":true,\"print_exposure\":1.25}";
            const spk_status status = spk_set_params(session.get(), delta.c_str(), nullptr);
            const std::string error = spk_last_error(engine.get());
            require(status == SPK_ERR_USER && error.find(feature.label) != std::string::npos &&
                    error.find("Windows Vulkan backend") != std::string::npos,
                    std::string("unclear feature refusal: ") + feature.key + ": " + error);
            require(params(session.get()) == before, std::string("refused delta mutated params: ") + feature.key);
            render(engine.get(), session.get());

            spk_session* rejected = spk_open(engine.get(), &input, delta.c_str(), nullptr);
            const std::string open_error = spk_last_error(engine.get());
            if (rejected) spk_session_release(rejected);
            require(!rejected && open_error.find(feature.label) != std::string::npos,
                    std::string("open accepted an unported effect: ") + feature.key);
        }

        spk_result di{};
        char* di_json = nullptr;
        const spk_status di_status = spk_render_digital_intermediate(session.get(), &di, &di_json);
        const std::string di_error = spk_last_error(engine.get());
        const bool empty = !di.texture && !di.rgba16 && !di_json;
        spk_result_free(&di);
        spk_string_free(di_json);
        require(di_status == SPK_ERR_USER && empty &&
                di_error.find("digital intermediate") != std::string::npos,
                "DI API did not return a clear refusal and empty result");
        require(params(session.get()) == before, "DI refusal mutated the session");
        render(engine.get(), session.get());

        const float* mask = nullptr;
        uint32_t width = 0, height = 0;
        require(spk_contrast_mask_field(session.get(), "full", &mask, &width, &height) == SPK_OK &&
                !mask && width == 0 && height == 0, "disabled contrast mask must have an empty field");

        // Analysis must remain read-only, even now that its curve can render.
        char* latitude_json = nullptr;
        const spk_status latitude_status = spk_scene_latitude(session.get(), nullptr, &latitude_json);
        const std::string latitude_error = spk_last_error(engine.get());
        const bool latitude_valid = latitude_json &&
            std::string(latitude_json).find("\"medium\"") != std::string::npos &&
            std::string(latitude_json).find("\"scene\"") != std::string::npos;
        spk_string_free(latitude_json);
        require(latitude_status == SPK_OK && latitude_valid, "latitude analysis failed: " + latitude_error);
        require(params(session.get()) == before, "latitude analysis mutated the session");
        render(engine.get(), session.get());
        // A wide neutral ramp makes both tone features observable. Compare
        // identical deterministic inputs so random grain cannot hide a no-op.
        for (size_t i = 0; i < pixels.size() / 3; ++i) {
            const float v = float(0.0002 * std::pow(2.0, double(i % 32) / 2.0));
            pixels[3 * i] = v; pixels[3 * i + 1] = v; pixels[3 * i + 2] = v;
        }
        const char* deterministic = R"({"film_stock":"kodak_portra_400","print_stock":"kodak_portra_endura","input_color_space":"ProPhoto RGB","input_cctf_decoding":false,"grain_active":false,"glare_active":false,"auto_exposure":false})";
        std::unique_ptr<spk_session, decltype(&spk_session_release)> tones(
            spk_open(engine.get(), &input, deterministic, nullptr), spk_session_release);
        require(bool(tones), "tone session open failed");
        auto pixels_of = [&](bool reprint, int expected_cache) {
            spk_result result{};
            const auto status = reprint ? spk_reprint(tones.get(), "full", &result) : spk_render(tones.get(), "full", &result);
            const std::string error = spk_last_error(engine.get());
            std::vector<uint16_t> copy;
            if (status == SPK_OK && result.rgba16) {
                for (uint32_t y = 0; y < result.height; ++y)
                    copy.insert(copy.end(), result.rgba16 + size_t(y) * result.row_stride_px * 4,
                                result.rgba16 + (size_t(y) * result.row_stride_px + result.width) * 4);
            }
            const bool cached = result.negative_was_cached != 0;
            spk_result_free(&result);
            require(status == SPK_OK && !copy.empty(), "tone render failed: " + error);
            require(expected_cache < 0 || cached == bool(expected_cache), "incorrect tone negative-cache invalidation");
            return copy;
        };
        auto edit = [&](const char* delta) {
            const auto status = spk_set_params(tones.get(), delta, nullptr);
            require(status == SPK_OK, std::string("tone parameters refused: ") + spk_last_error(engine.get()));
        };
        const auto baseline = pixels_of(false, 0);
        edit(R"({"scene_latitude_active":true,"scene_latitude_highlight_knee":1,"scene_latitude_highlight_room":1,"scene_latitude_shadow_knee":-1,"scene_latitude_shadow_room":1})");
        const auto placed = pixels_of(false, 0);
        require(placed != baseline, "scene placement rendered no change");
        edit(R"({"scene_latitude_active":false})");
        require(pixels_of(false, 0) == baseline, "scene placement bypass changed default pixels");
        edit(R"({"contrast_mask_active":true,"contrast_mask_highlights":1,"contrast_mask_shadows":1,"contrast_mask_core":0.5})");
        const auto masked = pixels_of(true, 1);
        require(masked != baseline, "contrast mask rendered no change");
        require(spk_contrast_mask_field(tones.get(), "full", &mask, &width, &height) == SPK_OK && mask && width && height,
                "enabled contrast mask has no analysis field");
        require(pixels_of(true, 1) == masked, "mask analysis changed the next print");
        edit(R"({"contrast_mask_active":false})");
        require(pixels_of(true, 1) == baseline, "contrast mask bypass changed default pixels");
        std::cout << "Windows feature boundaries passed (3 refusals, latitude mapping, contrast mask, cache and bypass)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
