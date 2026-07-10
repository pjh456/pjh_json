#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"
#include <cstring>

namespace pjh::json
{
    /*
     * Parse JSON literal (true / false / null)
     *
     * Uses bit_cast to precompute integer magic constants for the expected
     * byte sequences, then compares via memcpy. This avoids strcmp and
     * is safe for unaligned access on all modern platforms.
     */
    /*
     * Build lookup table for valid bytes after a literal terminator.
     * Only whitespace, structural chars (, : } ]), and NUL are valid.
     */
    static constexpr auto make_valid_after_literal()
    {
        std::array<bool, 256> t{};
        t[0] = t[9] = t[10] = t[13] = true;   // NUL \t \n \r
        t[32] = t[44] = t[58] = t[93] = t[125] = true; // space , : ] }
        return t;
    }
    static constexpr auto kValidAfterLiteral = make_valid_after_literal();

    Json Parser::parse_literal()
    {
        Json result;

        uint32_t val32;
        std::memcpy(&val32, m_curr, 4);

        if (val32 == kTrueMagic)
        {
            m_curr += 4;
            result = Json(true);
        }
        else if (val32 == kNullMagic)
        {
            m_curr += 4;
            result = Json(nullptr);
        }
        else
        {
            uint64_t val64;
            std::memcpy(&val64, m_curr, 8);
            if ((val64 & kFalseMask) == kFalseMagic)
            {
                m_curr += 5;
                result = Json(false);
            }
            else
            {
                throw_parse_error("Invalid literal, expected true/false/null", m_curr, m_begin);
            }
        }

        // Reject trailing garbage after a literal.
        uint8_t next = static_cast<uint8_t>(*m_curr);
        if (!kValidAfterLiteral[next])
        {
            throw_parse_error("Invalid literal, unexpected characters after true/false/null", m_curr, m_begin);
        }

        return result;
    }
}
