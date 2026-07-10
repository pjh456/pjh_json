#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"
#include "internal.hpp"
#include <xsimd/xsimd.hpp>

namespace pjh::json
{
    /*
     * Skip whitespace using SIMD
     *
     * 1. Quick scalar check: if current byte > 0x20, it is not WS -> return.
     * 2. SIMD loop: load a batch, compute WS mask (bytes <= 0x20 and != 0).
     * 3. Find the first non-WS byte via ctz on the complement mask.
     * 4. Advance cursor past it and return.
     */
    void Parser::skip_whitespace()
    {
        if (static_cast<uint8_t>(*m_curr) > 0x20)
            return;

        using batch_type = xsimd::batch<uint8_t>;
        std::size_t batch_size = batch_type::size;
        static_assert(
            batch_type::size <= 64,
            "batch_size too large for uint64_t mask");

        auto ctrl_space = xsimd::broadcast<uint8_t>(0x20);
        auto zero = xsimd::broadcast<uint8_t>(0);

        while (true)
        {
            auto b = batch_type::load_unaligned(
                reinterpret_cast<const uint8_t *>(m_curr));
            auto is_ws = (b <= ctrl_space) & (b != zero);

            uint64_t mask = is_ws.mask();
            uint64_t non_ws_mask = ~mask;
            if constexpr (batch_type::size < 64)
            {
                non_ws_mask &= (1ULL << batch_size) - 1;
            }

            if (non_ws_mask != 0)
            {
                m_curr += std::countr_zero(non_ws_mask);
                return;
            }
            m_curr += batch_size;
        }
    }

    /*
     * Read 4 hex digits at current position (advances cursor)
     */
    uint32_t Parser::parse_hex4()
    {
        uint32_t code = 0;
        for (int i = 0; i < 4; ++i)
        {
            char c = *m_curr++;
            code <<= 4;
            if (c >= '0' && c <= '9')
                code |= (c - '0');
            else if (c >= 'a' && c <= 'f')
                code |= (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                code |= (c - 'A' + 10);
            else
                throw ParseError("Invalid hex digit in unicode escape");
        }
        return code;
    }

    /*
     * Encode Unicode codepoint to UTF-8 bytes at dst and advance dst
     *
     * 1-byte: U+0000 - U+007F
     * 2-byte: U+0080 - U+07FF
     * 3-byte: U+0800 - U+FFFF
     * 4-byte: U+10000 - U+10FFFF
     */
    void encode_utf8(uint32_t cp, char *&dst)
    {
        if (cp <= 0x7F)
        {
            *dst++ = static_cast<char>(cp);
        }
        else if (cp <= 0x7FF)
        {
            *dst++ = static_cast<char>(0xC0 | ((cp >> 6) & 0x1F));
            *dst++ = static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp <= 0xFFFF)
        {
            *dst++ = static_cast<char>(0xE0 | ((cp >> 12) & 0x0F));
            *dst++ = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            *dst++ = static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp <= 0x10FFFF)
        {
            *dst++ = static_cast<char>(0xF0 | ((cp >> 18) & 0x07));
            *dst++ = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            *dst++ = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            *dst++ = static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            throw ParseError("Invalid unicode codepoint");
        }
    }

    /*
     * Handle JSON escape sequence and write decoded character to dst
     *
     * 1. Consume the backslash, dispatch on the next character.
     * 2. Simple escapes (" \\ / b f n r t) write the corresponding char.
     * 3. Unicode '\uXXXX': parse hex4, handle surrogate pairs,
     *    then encode as UTF-8.
     */
    void handle_escape(char *&dst, const char *&m_curr, Parser &parser)
    {
        ++m_curr;
        switch (*m_curr)
        {
        case '"':
            *dst++ = '"';
            break;
        case '\\':
            *dst++ = '\\';
            break;
        case '/':
            *dst++ = '/';
            break;
        case 'b':
            *dst++ = '\b';
            break;
        case 'f':
            *dst++ = '\f';
            break;
        case 'n':
            *dst++ = '\n';
            break;
        case 'r':
            *dst++ = '\r';
            break;
        case 't':
            *dst++ = '\t';
            break;
        case 'u':
        {
            ++m_curr;
            uint32_t cp = parser.parse_hex4();

            // High surrogate (U+D800-U+DBFF): expect a low surrogate pair
            if (cp >= 0xD800 && cp <= 0xDBFF)
            {
                if (m_curr[0] == '\\' && m_curr[1] == 'u')
                {
                    m_curr += 2;
                    uint32_t cp2 = parser.parse_hex4();
                    if (cp2 >= 0xDC00 && cp2 <= 0xDFFF)
                        cp = 0x10000 + (((cp - 0xD800) << 10) | (cp2 - 0xDC00));
                    else
                        throw ParseError("Invalid surrogate pair");
                }
                else
                    throw ParseError("Expected low surrogate");
            }
            encode_utf8(cp, dst);
            return;
        }
        default:
            throw ParseError("Invalid escape character");
        }
        ++m_curr;
    }
}
