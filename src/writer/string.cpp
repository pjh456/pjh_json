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

    static void append_u4(std::pmr::string &sink, uint16_t v)
    {
        char buf[6] = {'\\', 'u', 0, 0, 0, 0};
        buf[2] = HEX[(v >> 12) & 0xF];
        buf[3] = HEX[(v >> 8) & 0xF];
        buf[4] = HEX[(v >> 4) & 0xF];
        buf[5] = HEX[v & 0xF];
        sink.append(buf, 6);
    }

    static void write_escaped_ascii(std::pmr::string &sink, std::string_view s)
    {
        const auto *p = reinterpret_cast<const uint8_t *>(s.data());
        const auto *end = p + s.size();
        while (p < end)
        {
            uint8_t c = *p;
            if (c < 0x20 || c == '"' || c == '\\')
            {
                append_escape(sink, static_cast<char>(c));
                ++p;
            }
            else if (c < 0x80)
            {
                sink.push_back(static_cast<char>(c));
                ++p;
            }
            else
            {
                // decode a UTF-8 sequence to a codepoint
                uint32_t cp;
                int len;
                if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
                else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
                else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
                else throw JsonError("Invalid UTF-8 lead byte in string");

                if (p + len > end)
                    throw JsonError("Truncated UTF-8 sequence in string");
                for (int i = 1; i < len; ++i)
                {
                    if ((p[i] & 0xC0) != 0x80)
                        throw JsonError("Invalid UTF-8 continuation byte in string");
                    cp = (cp << 6) | (p[i] & 0x3F);
                }
                p += len;

                if (cp <= 0xFFFF)
                {
                    append_u4(sink, static_cast<uint16_t>(cp));
                }
                else
                {
                    cp -= 0x10000;
                    append_u4(sink, static_cast<uint16_t>(0xD800 + (cp >> 10)));
                    append_u4(sink, static_cast<uint16_t>(0xDC00 + (cp & 0x3FF)));
                }
            }
        }
    }

    // Escape and append s to sink, wrapped in double quotes.
    void write_escaped(std::pmr::string &sink, std::string_view s, bool ascii)
    {
        sink.push_back('"');

        if (ascii)
        {
            write_escaped_ascii(sink, s);
            sink.push_back('"');
            return;
        }

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
