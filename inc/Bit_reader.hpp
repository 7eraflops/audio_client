#pragma once

#include <cstdint>
#include <stdexcept>

template <typename Input_stream>
class Bit_reader
{
private:
    Input_stream *m_stream{};
    uint64_t m_bit_buffer{};
    uint8_t m_bits_in_buffer{};

public:
    explicit Bit_reader(Input_stream &stream)
        : m_stream(&stream), m_bit_buffer(0), m_bits_in_buffer(0) {}

    bool eos() const
    {
        if (m_stream->peek() == EOF)
        {
            return true;
        }
        return false;
    }

    uint8_t get_byte()
    {
        if (m_stream == nullptr || m_stream->eof())
        {
            throw std::runtime_error("End of stream reached.");
        }

        char byte;
        if (!m_stream->get(byte))
        {
            throw std::runtime_error("Failed to read byte from stream.");
        }
        return static_cast<uint8_t>(byte);
    }

    uint64_t read_bits_unsigned(uint8_t num_bits)
    {
        if (num_bits > 64)
        {
            throw std::invalid_argument("Number of bits to read must be between 1 and 64.");
        }

        if (num_bits == 0)
        {
            return 0;
        }

        while (m_bits_in_buffer < num_bits)
        {
            uint8_t byte = get_byte();
            m_bit_buffer = (m_bit_buffer << 8) | byte;
            m_bits_in_buffer += 8;
        }

        uint64_t result = (m_bit_buffer >> (m_bits_in_buffer - num_bits)) & ((1ULL << num_bits) - 1);
        m_bits_in_buffer -= num_bits;

        return result;
    }

    int64_t read_bits_signed(uint8_t num_bits)
    {
        uint64_t result = read_bits_unsigned(num_bits);

        if (result & (1ULL << (num_bits - 1)))
        {
            return static_cast<int64_t>(result) - (1ULL << num_bits);
        }
        return static_cast<int64_t>(result);
    }

    void align_to_byte()
    {
        m_bits_in_buffer -= m_bits_in_buffer % 8;
    }
};
