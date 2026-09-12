#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"
#include "pjh_json/detail/utils.hpp"
#include "pjh_json/grammar.hpp"
#include "unicode.hpp"
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
        // JSON5 mode replaces the RFC whitespace class with
        // whitespace + comments. It is a distinct, m_end-bounded scanner;
        // the RFC SIMD fast path below is not used (and not changed).
        if (m_json5)
        {
            skip_json5_trivia();
            return;
        }

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

    /*
     * Skip JSON5 trivia (task 40.1, Unicode-extended by 40.5): white space +
     * comments.
     *
     * Every loop condition is explicitly m_curr/m_end-bounded. This is
     * load-bearing: parse_jsonl builds a Parser over a LINE SUB-VIEW of
     * the whole padded buffer, so past m_end sit the NEXT line's bytes,
     * not NUL padding. An unbounded scan (relying on padding) would let a
     * JSON5 blank/comment line consume the following line — recreating
     * the task-04 `[[1],[1]]` silent-duplication bug.
     *
     * White space is the JSON5 1.0.0 §8 set: the four RFC bytes plus VT/FF
     * (grammar::is_json5_whitespace_ascii), and the multi-byte NBSP/LS/PS/
     * U+FEFF/Zs code points. The latter are decoded by the strict, bounded
     * unicode::decode_utf8, so a malformed sequence is not skipped and can
     * never be classified as trivia.
     *
     * Line comment: two slashes up to a LineTerminator (LF/CR, or the
     * multi-byte U+2028/U+2029) or m_end. Block comment: slash-star to the
     * first star-slash; non-nesting per JSON5 1.0.0. An unterminated block
     * comment records UnexpectedEndOfInput at m_end (reusing an existing
     * ErrorCode: zero new codes).
     */
    void Parser::skip_json5_trivia()
    {
        while (true)
        {
            while (m_curr < m_end && grammar::is_json5_whitespace_ascii(static_cast<unsigned char>(*m_curr)))
                ++m_curr;

            if (m_curr < m_end && static_cast<unsigned char>(*m_curr) >= 0x80)
            {
                uint32_t cp = 0;
                std::size_t len = 0;
                if (unicode::decode_utf8(m_curr, m_end, cp, len) && grammar::is_json5_whitespace(cp))
                {
                    m_curr += len;
                    continue;
                }
            }

            if (m_curr + 1 < m_end && m_curr[0] == '/' && m_curr[1] == '/')
            {
                m_curr += 2;
                while (m_curr < m_end && *m_curr != '\n' && *m_curr != '\r')
                {
                    if (m_curr + 2 < m_end && static_cast<unsigned char>(m_curr[0]) == 0xE2 &&
                        static_cast<unsigned char>(m_curr[1]) == 0x80 &&
                        (static_cast<unsigned char>(m_curr[2]) == 0xA8 ||
                         static_cast<unsigned char>(m_curr[2]) == 0xA9))
                        break; // U+2028 LINE SEPARATOR / U+2029 PARAGRAPH SEPARATOR
                    ++m_curr;
                }
                continue;
            }

            if (m_curr + 1 < m_end && m_curr[0] == '/' && m_curr[1] == '*')
            {
                m_curr += 2;
                while (m_curr + 1 < m_end && !(m_curr[0] == '*' && m_curr[1] == '/'))
                    ++m_curr;
                if (m_curr + 1 >= m_end)
                {
                    // No closing `*` `/` before the logical end.
                    fail(ErrorCode::UnexpectedEndOfInput, m_end);
                    return;
                }
                m_curr += 2;
                continue;
            }

            return;
        }
    }

    uint32_t Parser::parse_hex4()
    {
        uint32_t out = 0;
        if (!parse_hex4(out))
            throw ParseError(m_error);
        return out;
    }

    bool Parser::parse_hex4(uint32_t &out)
    {
        const char *err_pos = nullptr;
        ErrorCode c = pjh::json::parse_hex4(m_curr, out, err_pos);
        if (c != ErrorCode::None)
        {
            fail(c, err_pos);
            return false;
        }
        return true;
    }

    /*
     * Read 4 hex digits at current position (advances cursor)
     *
     * On success returns ErrorCode::None and fills out; on failure returns
     * the key and sets err_pos to the offending digit.
     */
    ErrorCode parse_hex4(const char *&curr, uint32_t &out, const char *&err_pos)
    {
        uint32_t code = 0;
        for (int i = 0; i < 4; ++i)
        {
            char c = *curr++;
            int v = grammar::hex_value(c);
            if (v < 0)
            {
                // `curr` was advanced by *curr++ above; the offending digit is
                // the previous byte. Report that position, not the one past it.
                err_pos = curr - 1;
                return ErrorCode::InvalidHexDigit;
            }
            code = (code << 4) | static_cast<uint32_t>(v);
        }
        out = code;
        return ErrorCode::None;
    }

    /*
     * Encode Unicode codepoint to UTF-8 bytes at dst and advance dst
     *
     * 1-byte: U+0000 - U+007F
     * 2-byte: U+0080 - U+07FF
     * 3-byte: U+0800 - U+FFFF
     * 4-byte: U+10000 - U+10FFFF
     *
     * @return false when cp is outside the Unicode range (InvalidCodepoint,
     *         context-free)
     */
    bool encode_utf8(uint32_t cp, char *&dst)
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
            return false;
        }
        return true;
    }

    /*
     * Handle JSON escape sequence and write decoded character to dst
     *
     * 1. Consume the backslash, dispatch on the next character.
     * 2. Simple escapes (" \\ / b f n r t) write the corresponding char.
     * 3. Unicode '\uXXXX': parse hex4, handle surrogate pairs,
     *    then encode as UTF-8.
     *
     * @return ErrorCode::None on success; on failure the key is returned
     *         and err_pos is the offending byte (nullptr for the
     *         context-free InvalidCodepoint).
     */
    ErrorCode handle_escape(char *&dst, const char *&m_curr, const char *&err_pos)
    {
        ++m_curr;
        char decoded = grammar::short_escape_value(*m_curr);
        if (decoded != '\0')
        {
            *dst++ = decoded;
            ++m_curr;
            return ErrorCode::None;
        }
        if (*m_curr != 'u')
        {
            err_pos = m_curr;
            return ErrorCode::InvalidEscapeChar;
        }

        ++m_curr;
        uint32_t cp = 0;
        {
            const char *ep = nullptr;
            ErrorCode ec = pjh::json::parse_hex4(m_curr, cp, ep);
            if (ec != ErrorCode::None)
            {
                err_pos = ep;
                return ec;
            }
        }

        // High surrogate (U+D800-U+DBFF): expect a low surrogate pair
        if (grammar::is_high_surrogate(cp))
        {
            if (m_curr[0] == '\\' && m_curr[1] == 'u')
            {
                m_curr += 2;
                uint32_t cp2 = 0;
                const char *ep = nullptr;
                ErrorCode ec = pjh::json::parse_hex4(m_curr, cp2, ep);
                if (ec != ErrorCode::None)
                {
                    err_pos = ep;
                    return ec;
                }
                if (grammar::is_low_surrogate(cp2))
                    cp = 0x10000 + (((cp - 0xD800) << 10) | (cp2 - 0xDC00));
                else
                {
                    err_pos = m_curr;
                    return ErrorCode::InvalidSurrogatePair;
                }
            }
            else
            {
                err_pos = m_curr;
                return ErrorCode::ExpectedLowSurrogate;
            }
        }
        // Lone low surrogate (U+DC00-U+DFFF) not allowed by RFC 8259 §7
        else if (grammar::is_low_surrogate(cp))
        {
            err_pos = m_curr;
            return ErrorCode::LoneLowSurrogate;
        }
        if (!encode_utf8(cp, dst))
        {
            err_pos = nullptr;
            return ErrorCode::InvalidCodepoint;
        }
        return ErrorCode::None;
    }
}
