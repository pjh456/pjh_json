#include "pjh_json/writer.hpp"
#include <bit>
#include <cstdint>
#include <xsimd/xsimd.hpp>

namespace pjh::json
{
    static constexpr char HEX[] = "0123456789abcdef";

    static void append_escape(std::pmr::string &sink, char c)
    {
        switch (c)
        {
        case '"':
            sink.append("\\\"");
            break;
        case '\\':
            sink.append("\\\\");
            break;
        case '\b':
            sink.append("\\b");
            break;
        case '\f':
            sink.append("\\f");
            break;
        case '\n':
            sink.append("\\n");
            break;
        case '\r':
            sink.append("\\r");
            break;
        case '\t':
            sink.append("\\t");
            break;
        default:
        {
            // other control chars < 0x20 -> \u00XX
            char buf[6] = {'\\', 'u', '0', '0', 0, 0};
            buf[4] = HEX[(static_cast<uint8_t>(c) >> 4) & 0xF];
            buf[5] = HEX[static_cast<uint8_t>(c) & 0xF];
            sink.append(buf, 6);
            break;
        }
        }
    }

    // Escape and append s to sink, wrapped in double quotes.
    void write_escaped(std::pmr::string &sink, std::string_view s)
    {
        sink.push_back('"');

        const char *curr = s.data();
        const char *end = s.data() + s.size();

        using batch_type = xsimd::batch<uint8_t>;
        constexpr std::size_t batch_size = batch_type::size;
        auto quote = xsimd::broadcast<uint8_t>('"');
        auto escape = xsimd::broadcast<uint8_t>('\\');
        auto ctrl = xsimd::broadcast<uint8_t>(0x20);

        const char *run = curr; // start of current clean (copy-verbatim) run

        // SIMD bulk scan over full batches
        while (curr + batch_size <= end)
        {
            auto b = batch_type::load_unaligned(
                reinterpret_cast<const uint8_t *>(curr));
            auto matches = (b == quote) | (b == escape) | (b < ctrl);
            uint64_t mask = matches.mask();

            if constexpr (batch_size < 64)
                mask &= (1ULL << batch_size) - 1;

            while (mask != 0)
            {
                int off = std::countr_zero(mask);
                const char *hit = curr + off;
                if (hit > run)
                    sink.append(run, hit - run);
                append_escape(sink, *hit);
                run = hit + 1;
                mask &= mask - 1; // clear lowest set bit
            }
            curr += batch_size;
        }

        // scalar tail
        while (curr < end)
        {
            char c = *curr;
            if (c == '"' || c == '\\' || static_cast<uint8_t>(c) < 0x20)
            {
                if (curr > run)
                    sink.append(run, curr - run);
                append_escape(sink, c);
                run = curr + 1;
            }
            ++curr;
        }

        if (end > run)
            sink.append(run, end - run);

        sink.push_back('"');
    }
}
