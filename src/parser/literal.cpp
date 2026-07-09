#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"
#include <bit>
#include <cstring>

namespace pjh::json
{
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

        uint32_t val32;
        std::memcpy(&val32, m_curr, 4);

        if (val32 == true_magic)
        {
            m_curr += 4;
            return Json(true);
        }
        if (val32 == null_magic)
        {
            m_curr += 4;
            return Json(nullptr);
        }

        uint64_t val64;
        std::memcpy(&val64, m_curr, 8);
        if ((val64 & false_mask) == false_magic)
        {
            m_curr += 5;
            return Json(false);
        }

        throw ParseError("Invalid literal");
    }
}
