#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"
#include "pjh_json/detail/utils.hpp"
#include "pjh_json/grammar.hpp"
#include <xsimd/xsimd.hpp>

namespace pjh::json
{
    /*
     * Skip whitespace using SIMD
     *
     * RFC 8259 §2: whitespace is exactly SPACE (0x20), TAB (0x09),
     * LF (0x0A), CR (0x0D) — no other byte, in particular no other
     * C0 control character.
     *
     * 1. Quick scalar check: if current byte > 0x20, it is not WS ->
     *    return (every legal WS byte is <= 0x20, so this stays a
     *    1-byte fast path).
     * 2. SIMD loop: load a batch, mark lanes equal to one of the 4 bytes.
     * 3. Find the first non-WS byte via ctz on the complement mask.
     * 4. Advance cursor past it and return. The kPaddingWidth NUL padding
     *    always stops the loop (NUL is not WS).
     */
    void Parser::skip_whitespace()
    {
        // The SIMD loop below broadcasts the four RFC 8259 §2 whitespace
        // bytes by hand; these pins keep the wide-mask set identical to the
        // shared grammar rule (single source of truth).
        static_assert(grammar::is_whitespace(0x20) &&
                      grammar::is_whitespace(0x09) &&
                      grammar::is_whitespace(0x0A) &&
                      grammar::is_whitespace(0x0D),
                      "SIMD whitespace set must match grammar::is_whitespace");
        static_assert(!grammar::is_whitespace(0x00) &&
                      !grammar::is_whitespace(0x0B) &&
                      !grammar::is_whitespace(0x0C) &&
                      !grammar::is_whitespace(0x1F),
                      "non-whitespace controls must stay rejected");

        if (static_cast<uint8_t>(*m_curr) > 0x20)
            return;

        using batch_type = xsimd::batch<uint8_t>;
        std::size_t batch_size = batch_type::size;
        static_assert(
            batch_type::size <= 64,
            "batch_size too large for uint64_t mask");
        static_assert(
            2 * batch_type::size <= kPaddingWidth,
            "padding must keep 2x SIMD batch headroom");

        auto space = xsimd::broadcast<uint8_t>(0x20);
        auto tab = xsimd::broadcast<uint8_t>(0x09);
        auto nl = xsimd::broadcast<uint8_t>(0x0A);
        auto cr = xsimd::broadcast<uint8_t>(0x0D);

        while (true)
        {
            auto b = batch_type::load_unaligned(
                reinterpret_cast<const uint8_t *>(m_curr));
            auto is_ws = (b == space) | (b == tab) | (b == nl) | (b == cr);

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

    uint32_t Parser::parse_hex4()
    {
        return pjh::json::parse_hex4(m_curr, m_begin);
    }

    /*
     * Read 4 hex digits at current position (advances cursor)
     */
    uint32_t parse_hex4(const char *&curr, const char *begin)
    {
        uint32_t code = 0;
        for (int i = 0; i < 4; ++i)
        {
            char c = *curr++;
            int v = grammar::hex_value(c);
            if (v < 0)
                // `curr` was advanced by *curr++ above; the offending digit is
                // the previous byte. Report that position, not the one past it.
                throw_parse_error("Invalid hex digit in unicode escape", curr - 1, begin);
            code = (code << 4) | static_cast<uint32_t>(v);
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
    void handle_escape(char *&dst, const char *&m_curr, const char *m_begin)
    {
        ++m_curr;
        char decoded = grammar::short_escape_value(*m_curr);
        if (decoded != '\0')
        {
            *dst++ = decoded;
            ++m_curr;
            return;
        }
        if (*m_curr != 'u')
            throw_parse_error("Invalid escape character", m_curr, m_begin);

        ++m_curr;
        uint32_t cp = parse_hex4(m_curr, m_begin);

        // High surrogate (U+D800-U+DBFF): expect a low surrogate pair
        if (grammar::is_high_surrogate(cp))
        {
            if (m_curr[0] == '\\' && m_curr[1] == 'u')
            {
                m_curr += 2;
                uint32_t cp2 = parse_hex4(m_curr, m_begin);
                if (grammar::is_low_surrogate(cp2))
                    cp = 0x10000 + (((cp - 0xD800) << 10) | (cp2 - 0xDC00));
                else
                    throw_parse_error("Invalid surrogate pair", m_curr, m_begin);
            }
            else
                throw_parse_error("Expected low surrogate", m_curr, m_begin);
        }
        // Lone low surrogate (U+DC00-U+DFFF) not allowed by RFC 8259 §7
        else if (grammar::is_low_surrogate(cp))
            throw_parse_error("Lone low surrogate, expected high surrogate first", m_curr, m_begin);
        encode_utf8(cp, dst);
    }
}
