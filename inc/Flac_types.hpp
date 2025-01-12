#pragma once

#include <cstdint>
#include <unordered_map>

using buffer_sample_type = int64_t;

struct Stream_info
{
    uint16_t min_block_size{};
    uint16_t max_block_size{};
    uint32_t min_frame_size{};
    uint32_t max_frame_size{};
    uint32_t sample_rate{};
    uint8_t channels{};
    uint8_t bits_per_sample{};
    uint64_t total_samples{};
};

struct Frame_info
{
    uint8_t blocking_strategy{};
    uint16_t block_size{};
    uint32_t sample_rate{};
    uint8_t channel_assignment{};
    uint8_t bits_per_sample{};
    uint64_t frame_or_sample_number{};
    uint8_t crc_8{};
    uint16_t crc_16{};
};

struct Vorbis_comment
{
    std::string vendor_string;
    std::unordered_map<std::string, std::string> user_comments;
};

enum class block_type : uint8_t
{
    STREAMINFO = 0,
    PADDING = 1,
    APPLICATION = 2,
    SEEKTABLE = 3,
    VORBIS_COMMENT = 4,
    CUESHEET = 5,
    PICTURE = 6
};
