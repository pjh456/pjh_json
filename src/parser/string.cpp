#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"
#include "pjh_json/detail/utils.hpp"
#include "pjh_json/detail/utf8.hpp"
#include "pjh_json/grammar.hpp"
#include <xsimd/xsimd.hpp>

namespace pjh::json
{
    namespace
    {
        /*
         * Bounded 4-hex-digit read for the JSON5 string path.
         *
         * The RFC parse_hex4 (detail/utils.hpp) reads four bytes
         * unconditionally; that is only safe because the RFC caller works
         * on a padded whole buffer. Here a line sub-view may end mid-escape,
         * so every byte is checked against `end` first. A truncated escape
         * reports UnterminatedString (JSON5 mode reuses existing codes).
         */
        ErrorCode read_hex4_bounded(const char *&curr, const char *end, uint32_t &out, const char *&err_pos)
        {
            uint32_t code = 0;
            for (int i = 0; i < 4; ++i)
            {
                if (curr >= end)
                {
                    err_pos = curr;
                    return ErrorCode::UnterminatedString;
                }
                int v = grammar::hex_value(*curr);
                if (v < 0)
                {
                    err_pos = curr;
                    return ErrorCode::InvalidHexDigit;
                }
                code = (code << 4) | static_cast<uint32_t>(v);
                ++curr;
            }
            out = code;
            return ErrorCode::None;
        }

        /*
         * m_end-bounded JSON5 1.0.0 §5.2 escape / line-continuation decoder.
         * Extends the RFC set (short escapes + \uXXXX with
         * surrogate pairs) with:
         *   - LineContinuation: `\` + LF/CR/CRLF/U+2028/U+2029 is removed
         *     entirely (writes no byte). CRLF is one LineTerminatorSequence,
         *     so a backslash before CR also eats an immediately following LF.
         *   - `\xHH`: exactly two hex digits -> one byte.
         *   - `\v` (VT) and `\'` single escapes (short_escape_value_json5).
         *   - `\0`: NUL, but only when not followed by a decimal digit (the
         *     octal-looking `\00`/`\01` forms stay rejected).
         *   - `\1`..`\9`: illegal (JSON5 has no octal escapes).
         *   - IdentityEscape: any other non-digit character yields itself.
         *
         * `end` bounds every read so a malformed escape at a parse_jsonl line
         * end cannot read the next line. On failure err_pos == nullptr means
         * context-free (InvalidCodepoint), matching handle_escape.
         */
        ErrorCode handle_escape_json5_bounded(char *&dst, const char *&curr, const char *end, const char *&err_pos)
        {
            ++curr; // consume the backslash
            if (curr >= end)
            {
                err_pos = curr;
                return ErrorCode::UnterminatedString;
            }
            const unsigned char e = static_cast<unsigned char>(*curr);

            // LineContinuation: LF, CR (CRLF as one sequence), U+2028/29.
            if (e == '\n')
            {
                ++curr;
                return ErrorCode::None;
            }
            if (e == '\r')
            {
                ++curr;
                if (curr < end && *curr == '\n')
                    ++curr;
                return ErrorCode::None;
            }
            if (e == 0xE2 && end - curr >= 3 && static_cast<unsigned char>(curr[1]) == 0x80 &&
                (static_cast<unsigned char>(curr[2]) == 0xA8 || static_cast<unsigned char>(curr[2]) == 0xA9))
            {
                curr += 3; // U+2028 LINE SEPARATOR / U+2029 PARAGRAPH SEPARATOR
                return ErrorCode::None;
            }

            // `\xHH`: exactly two hex digits.
            if (e == 'x')
            {
                ++curr;
                if (end - curr < 2)
                {
                    err_pos = curr;
                    return ErrorCode::UnterminatedString;
                }
                const int hi = grammar::hex_value(curr[0]);
                if (hi < 0)
                {
                    err_pos = curr;
                    return ErrorCode::InvalidHexDigit;
                }
                const int lo = grammar::hex_value(curr[1]);
                if (lo < 0)
                {
                    err_pos = curr + 1;
                    return ErrorCode::InvalidHexDigit;
                }
                *dst++ = static_cast<char>((hi << 4) | lo);
                curr += 2;
                return ErrorCode::None;
            }

            // `\0` is NUL unless a decimal digit follows (octal is absent).
            if (e == '0')
            {
                if (curr + 1 < end && curr[1] >= '0' && curr[1] <= '9')
                {
                    err_pos = curr;
                    return ErrorCode::InvalidEscapeChar;
                }
                *dst++ = '\0';
                ++curr;
                return ErrorCode::None;
            }

            // `\1`..`\9`: octal escapes are not part of JSON5.
            if (e >= '1' && e <= '9')
            {
                err_pos = curr;
                return ErrorCode::InvalidEscapeChar;
            }

            // `\uXXXX` with surrogate pairs (same decode as the RFC helper).
            if (e == 'u')
            {
                ++curr;
                uint32_t cp = 0;
                {
                    ErrorCode ec = read_hex4_bounded(curr, end, cp, err_pos);
                    if (ec != ErrorCode::None)
                        return ec;
                }

                if (grammar::is_high_surrogate(cp))
                {
                    if (end - curr >= 2 && curr[0] == '\\' && curr[1] == 'u')
                    {
                        curr += 2;
                        uint32_t cp2 = 0;
                        ErrorCode ec = read_hex4_bounded(curr, end, cp2, err_pos);
                        if (ec != ErrorCode::None)
                            return ec;
                        if (grammar::is_low_surrogate(cp2))
                            cp = 0x10000 + (((cp - 0xD800) << 10) | (cp2 - 0xDC00));
                        else
                        {
                            err_pos = curr;
                            return ErrorCode::InvalidSurrogatePair;
                        }
                    }
                    else
                    {
                        err_pos = curr;
                        return ErrorCode::ExpectedLowSurrogate;
                    }
                }
                else if (grammar::is_low_surrogate(cp))
                {
                    err_pos = curr;
                    return ErrorCode::LoneLowSurrogate;
                }
                if (!encode_utf8(cp, dst))
                {
                    err_pos = nullptr;
                    return ErrorCode::InvalidCodepoint;
                }
                return ErrorCode::None;
            }

            // Single escapes shared with RFC plus `\'` / `\v`.
            const char decoded = grammar::short_escape_value_json5(static_cast<char>(e));
            if (decoded != '\0')
            {
                *dst++ = decoded;
                ++curr;
                return ErrorCode::None;
            }

            // IdentityEscape: any remaining non-digit character (digits, x, u
            // and the line terminators were all handled above) yields itself.
            *dst++ = static_cast<char>(e);
            ++curr;
            return ErrorCode::None;
        }
    }

    /*
     * Parse JSON string (SIMD-accelerated):
     *
     * Phase 1 — SIMD scan:
     *   1. Load SIMD batch of bytes.
     *   2. Check each byte for quote("), escape(\), or control(<0x20).
     *   3. Use bitmask to find first matching position.
     *   4. If match is quote -> return string_view (fast path, no escape).
     *   5. If match is escape -> record position and fall through to scalar.
     *   6. If none match -> advance by batch size and continue.
     *
     * Phase 2 — Scalar in-situ decode (only when escape found):
     *   The destination buffer overlays the source (safe: escaped text
     *   is always shorter than the original). Decode escapes in-place
     *   until closing quote.
     */
    bool Parser::parse_string(String &out)
    {
        if (m_curr >= m_end || *m_curr != '"')
        {
            fail(ErrorCode::ExpectedQuote, m_curr);
            return false;
        }
        // JSON5 40.3 adds escapes and line continuations to double-quoted
        // strings too; the m_end-bounded scalar decoder covers them. The RFC
        // SIMD body below is byte-for-byte unchanged on the default path.
        if (m_json5)
            return parse_string_json5(out, '"');
        ++m_curr;

        const char *start = m_curr;
        char *dst = nullptr;

        using batch_type = xsimd::batch<uint8_t>;
        std::size_t batch_size = batch_type::size;
        static_assert(
            batch_type::size <= 64,
            "batch_size too large for uint64_t mask");
        static_assert(
            2 * batch_type::size <= kPaddingWidth,
            "padding must keep 2x SIMD batch headroom");
        auto quote = xsimd::broadcast<uint8_t>('"');
        auto escape = xsimd::broadcast<uint8_t>('\\');
        auto ctrl = xsimd::broadcast<uint8_t>(0x20);

        // Phase 1: SIMD scan
        while (true)
        {
            auto b = batch_type::load_unaligned(
                reinterpret_cast<const uint8_t *>(m_curr));
            auto matches = (b == quote) | (b == escape) | (b < ctrl);
            uint64_t mask = matches.mask();

            if constexpr (batch_type::size < 64)
                mask &= (1ULL << batch_size) - 1;

            if (mask != 0)
            {
                int skip = std::countr_zero(mask);
                m_curr += skip;

                // Fast path: closing quote, no escapes -> return borrowed view
                if (*m_curr == '"')
                {
                    if (m_strict_utf8)
                    {
                        const char *ep = nullptr;
                        ErrorCode ec = detail::check_utf8_strict(start, static_cast<size_t>(m_curr - start), ep);
                        if (ec != ErrorCode::None)
                        {
                            fail(ec, ep);
                            return false;
                        }
                    }
                    out = String(std::string_view(start, static_cast<size_t>(m_curr - start)));
                    ++m_curr;
                    return true;
                }
                // Escape found -> switch to in-situ decode
                else if (*m_curr == '\\')
                {
                    dst = const_cast<char *>(start) + (m_curr - start);
                    goto insitu_fallback;
                }
                // Control character -> error
                else
                {
                    if (m_curr >= m_end)
                    {
                        fail(ErrorCode::UnterminatedString, m_curr);
                        return false;
                    }
                    fail(ErrorCode::UnescapedControl, m_curr);
                    return false;
                }
            }
            else
                m_curr += batch_size;
        }

    // Phase 2: scalar fallback — decode escapes in-place
    // (safe: escaped forms expand to fewer bytes than source)
    insitu_fallback:
        // Declared after the label: the only entry into Phase 2 is the
        // goto above, so the ctor runs exactly once (a declaration before
        // the goto source would be bypassed on the jump — indeterminate
        // members).
        detail::Utf8Checker ck;
        if (m_strict_utf8)
            // Pre-escape raw run [start, first escape): Phase 2's loop
            // starts at the backslash, so cover the run explicitly (it
            // carries no quote/backslash/control — SIMD first-match).
            ck.feed_range(start, static_cast<size_t>(m_curr - start));
        if (ck.failed())
        {
            fail(ck.code, ck.err);
            return false;
        }
        while (true)
        {
            if (*m_curr == '"')
            {
                if (m_strict_utf8)
                    ck.end();
                if (ck.failed())
                {
                    fail(ck.code, ck.err);
                    return false;
                }
                ++m_curr;
                out = String(std::string_view(start, static_cast<size_t>(dst - start)));
                return true;
            }
            if (m_strict_utf8)
                ck.feed(static_cast<uint8_t>(*m_curr), m_curr);
            if (ck.failed())
            {
                fail(ck.code, ck.err);
                return false;
            }
            if (*m_curr == '\\')
            {
                const char *ep = nullptr;
                ErrorCode ec = handle_escape(dst, m_curr, ep);
                if (ec != ErrorCode::None)
                {
                    if (ep)
                        fail(ec, ep);
                    else
                        fail_context(ec); // InvalidCodepoint: context-free
                    return false;
                }
            }
            else if (static_cast<uint8_t>(*m_curr) < 0x20)
            {
                fail(ErrorCode::UnescapedControl, m_curr);
                return false;
            }
            else
            {
                *dst++ = *m_curr++;
            }
        }
    }

    /*
     * Parse a JSON5 string with either quote, scalar path.
     *
     * Deliberately a separate decoder from the double-quote SIMD path: the
     * RFC `"` scanner is byte-for-byte unchanged. parse_string() routes
     * here when the captured JSON5 mode is on, so double- and single-quoted
     * JSON5 strings share one grammar. The loop is explicitly m_end-bounded
     * at every step (needed for parse_jsonl line sub-views; see
     * skip_json5_trivia). It borrows the content on the no-escape fast path
     * and decodes escapes in place otherwise, exactly like parse_string's
     * scalar fallback.
     *
     * Grammar: the RFC escapes plus the JSON5-only forms in
     * handle_escape_json5_bounded (line continuations, `\xHH`, `\v`, `\0`,
     * `\'`, identity escapes). Raw StringCharacters are everything except
     * the quote, the backslash and the LineTerminators: raw LF/CR are
     * UnescapedControl, while every other raw byte (VT/FF/TAB, ...) is
     * accepted, matching JSON5's broader string body. Unescaped U+2028/29
     * pass through as ordinary raw bytes (JSON5 permits them).
     */
    bool Parser::parse_string_json5(String &out, char quote)
    {
        // Caller guarantees the leading quote (parse_string check / dispatch).
        ++m_curr;
        const char *start = m_curr;
        char *dst = nullptr;
        detail::Utf8Checker ck;

        while (true)
        {
            if (m_curr >= m_end)
            {
                fail(ErrorCode::UnterminatedString, m_curr);
                return false;
            }
            if (*m_curr == quote)
            {
                if (m_strict_utf8)
                    ck.end();
                if (ck.failed())
                {
                    fail(ck.code, ck.err);
                    return false;
                }
                const char *content_end = m_curr;
                ++m_curr;
                if (dst)
                    out = String(std::string_view(start, static_cast<size_t>(dst - start)));
                else
                    out = String(std::string_view(start, static_cast<size_t>(content_end - start)));
                return true;
            }
            if (m_strict_utf8)
                ck.feed(static_cast<uint8_t>(*m_curr), m_curr);
            if (ck.failed())
            {
                fail(ck.code, ck.err);
                return false;
            }
            if (*m_curr == '\\')
            {
                if (!dst)
                    dst = const_cast<char *>(m_curr);
                const char *ep = nullptr;
                ErrorCode ec = handle_escape_json5_bounded(dst, m_curr, m_end, ep);
                if (ec != ErrorCode::None)
                {
                    if (ep)
                        fail(ec, ep);
                    else
                        fail_context(ec); // InvalidCodepoint: context-free
                    return false;
                }
            }
            else if (grammar::is_json5_line_terminator_ascii(static_cast<uint8_t>(*m_curr)))
            {
                // Raw LF/CR is not a StringCharacter: it must be escaped or
                // reached through a `\` line continuation.
                fail(ErrorCode::UnescapedControl, m_curr);
                return false;
            }
            else
            {
                if (dst)
                    *dst++ = *m_curr;
                ++m_curr;
            }
        }
    }
}
