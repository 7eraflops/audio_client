#pragma once

#include <cstdint>

namespace Flac_constants
{
    static constexpr int16_t fixed_prediction_coefficients[5][4] = {
        {},
        {1},
        {2, -1},
        {3, -3, 1},
        {4, -6, 4, -1},
    };

    static constexpr uint32_t flac_marker = 0x664c6143; // "fLaC"

    static constexpr uint16_t frame_sync_code = 0b11111111111110;

    static constexpr uint16_t block_sizes[] = {
        0, 192, 576, 1152, 2304, 4608,
        0, 0, // Placeholders for variable-length cases
        256, 512, 1024, 2048, 4096, 8192, 16384, 32768};

    static constexpr uint32_t sample_rates[] = {
        0, // Reserved, use STREAMINFO sample rate
        88200, 176400, 192000, 8000, 16000, 22050,
        24000, 32000, 44100, 48000, 96000,
        0, // Placeholder for 8-bit sample rate
        0, // Placeholder for 16-bit sample rate (Hz)
        0, // Placeholder for 16-bit sample rate (10*Hz)
        0  // Invalid code
    };

    static constexpr uint8_t bits_per_sample_table[] = {
        0, // Reserved, use STREAMINFO bits per sample
        8, 12,
        0, // Reserved value
        16, 20, 24, 32};
}
