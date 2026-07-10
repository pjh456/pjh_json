#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"
#include <bit>
#include <cstring>

namespace pjh::json
{
    /*
     * Parse JSON literal (true / false / null)
     *
     * Uses bit_cast to precompute integer magic constants for the expected
     * byte sequences, then compares via memcpy. This avoids strcmp and
     * is safe for unaligned access on all modern platforms.
     *
     * 1. Read 4 bytes: compare against "true" and "null" as uint32.
     * 2. Read 8 bytes with a 5-byte mask: compare against "false".
     * 3. No match triggers an error.
     */
    Json Parser::parse_literal()
    {
        struct UChar4
        {
            uint8_t c[4];
        };
        constexpr uint32_t true_magic = std::bit_cast<uint32_t>(UChar4{'t', 'r', 'u', 'e'});
        constexpr uint32_t null_magic = std::bit_cast<uint32_t>(UChar4{'n', 'u', 'l', 'l'});

        struct UChar8
        {
            uint8_t c[8];
        };
        constexpr uint64_t false_magic = std::bit_cast<uint64_t>(UChar8{'f', 'a', 'l', 's', 'e', 0, 0, 0});
        constexpr uint64_t false_mask = std::bit_cast<uint64_t>(UChar8{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0});

        Json result;

        uint32_t val32;
        std::memcpy(&val32, m_curr, 4);

        if (val32 == true_magic)
        {
            m_curr += 4;
            result = Json(true);
        }
        else if (val32 == null_magic)
        {
            m_curr += 4;
            result = Json(nullptr);
        }
        else
        {
            uint64_t val64;
            std::memcpy(&val64, m_curr, 8);
            if ((val64 & false_mask) == false_magic)
            {
                m_curr += 5;
                result = Json(false);
            }
            else
            {
                throw_error("Invalid literal, expected true/false/null");
            }
        }

        // Reject trailing garbage: after a literal, only whitespace,
        // structural characters (, : } ]), or end-of-input are valid.
        uint8_t next = static_cast<uint8_t>(*m_curr);
        if (next != ' ' && next != '\t' && next != '\n' && next != '\r' &&
            next != ',' && next != ':' && next != '}' && next != ']' &&
            next != 0)
        {
            throw_error("Invalid literal, unexpected characters after true/false/null");
        }

        return result;
    }
}
