#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"
#include "pjh_json/detail/literal.hpp"
#include "pjh_json/detail/utils.hpp"
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

    bool Parser::parse_literal(Json &out)
    {
        uint32_t val32;
        std::memcpy(&val32, m_curr, 4);

        if (val32 == kTrueMagic)
        {
            m_curr += 4;
            out = Json(true);
        }
        else if (val32 == kNullMagic)
        {
            m_curr += 4;
            out = Json(nullptr);
        }
        else
        {
            uint64_t val64;
            std::memcpy(&val64, m_curr, 8);
            if ((val64 & kFalseMask) == kFalseMagic)
            {
                m_curr += 5;
                out = Json(false);
            }
            else
            {
                fail(ErrorCode::InvalidLiteral, m_curr);
                return false;
            }
        }

        // Reject trailing garbage after a literal. Under JSON5 the
        // following byte may legitimately be a comment or VT/FF whitespace
        // (which kValidAfterLiteral does not list); the outer trivia skip
        // and the structural/extra-character checks then take over. This
        // does NOT accept `truex`: with no trivia to skip, the caller's
        // trailing check / separator check still rejects it.
        if (!m_json5)
        {
            uint8_t next = static_cast<uint8_t>(*m_curr);
            if (!kValidAfterLiteral[next])
            {
                fail(ErrorCode::InvalidLiteralTrailing, m_curr);
                return false;
            }
        }

        return true;
    }
}
