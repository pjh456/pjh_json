#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"
#include "pjh_json/detail/utils.hpp"
#include "pjh_json/grammar.hpp"
#include <xsimd/xsimd.hpp>

namespace pjh::json
{
    namespace
    {
        /*
         * Strict UTF-8 gate (opt-in: Config::strict_utf8). Sequential
         * scalar state machine over RAW string content only:
         *   Phase 1 (no escapes): check_utf8_strict over [open+1, close)
         *     (the fast path guarantees no escape inside the window).
         *   Phase 2 (escapes): every non-quote byte is fed at its
         *     ORIGINAL source position in stream order — raw bytes and
         *     escape source text alike (escape source bytes are plain
         *     ASCII; feeding them turns a torn sequence — e.g. a 2-byte
         *     lead followed by a backslash — into a bad continuation at
         *     the backslash). The closing quote is never fed; end()
         *     reports a dangling sequence. Escape decode output (dst)
         *     is well-formed by construction (encode_utf8,
         *     src/parser/utils.cpp) and is never fed.
         * Rule set mirrors the writer's ascii-mode decoder
         * (src/writer/string.cpp write_escaped_ascii); the six message
         * classes are grep-identical on both sides. Offsets: first
         * offending byte — the bad byte itself for bad continuations,
         * the sequence lead byte for overlong / surrogate / >U+10FFFF /
         * truncated. All byte tests are uint8_t (0xE0 is negative as
         * signed char on two's-complement hosts).
         *
         * The checker no longer throws: it records the first failure
         * (ErrorCode + offending byte) and becomes a no-op afterwards
         * (first error wins, mirroring the old immediate stack unwind).
         */
        struct Utf8Checker
        {
            int need = 0;              // continuation bytes still expected
            int c1lo = 0, c1hi = 0;    // first-slot range (inclusive), valid while need > 0
            const char *lead = nullptr; // sequence start (offset anchor)
            ErrorCode m_fail_code = ErrorCode::None; // first-slot violation @ lead (else None)
            ErrorCode code = ErrorCode::None;        // first failure wins
            const char *err = nullptr;               // failure position

            [[nodiscard]] bool failed() const noexcept
            {
                return code != ErrorCode::None;
            }

            void set(ErrorCode c, const char *pos) noexcept
            {
                if (!failed())
                {
                    code = c;
                    err = pos;
                }
            }

            void feed(uint8_t b, const char *pos);  // records on first violation
            void end();                             // records a dangling sequence

            // Feed a raw run in stream order (Phase 2's pre-escape run
            // [start, first escape) needs the same coverage as the loop).
            // A run stops at the first recorded failure.
            void feed_range(const char *p, size_t n)
            {
                for (size_t i = 0; i < n && !failed(); ++i)
                    feed(static_cast<uint8_t>(p[i]), p + i);
            }
        };

        /*
         * First-slot two-tier check, per the rule table:
         * 1. Outside the general continuation range [0x80, 0xBF] =>
         *    "Invalid UTF-8 continuation byte in string" @ the bad byte
         *    (covers e.g. 0xE0 followed by 0xC0: the slot holds a lead,
         *    not a continuation).
         * 2. Inside [0x80, 0xBF] but outside the lead's c1 range => the
         *    violation classified at lead time (overlong for 0xE0/0xF0,
         *    surrogate for 0xED, >U+10FFFF for 0xF4) @ the lead.
         */
        void Utf8Checker::feed(uint8_t b, const char *pos)
        {
            if (failed())
                return;
            if (need == 0)
            {
                if (b <= 0x7F)
                    return; // 0x20-0x7F content (incl. DEL); <0x20 cannot
                            // reach this path (Phase 1 window is clean,
                            // Phase 2 control check reports below)
                if (b >= 0xC2 && b <= 0xDF)
                {
                    need = 1; c1lo = 0x80; c1hi = 0xBF; m_fail_code = ErrorCode::None;
                    lead = pos;
                    return;
                }
                if (b == 0xE0)
                {
                    need = 2; c1lo = 0xA0; c1hi = 0xBF; // 3-byte lead: two continuation slots
                    m_fail_code = ErrorCode::Utf8Overlong;
                    lead = pos;
                    return;
                }
                if (b == 0xED)
                {
                    need = 2; c1lo = 0x80; c1hi = 0x9F; // 3-byte lead: two continuation slots
                    m_fail_code = ErrorCode::Utf8Surrogate;
                    lead = pos;
                    return;
                }
                if ((b >= 0xE1 && b <= 0xEC) || b == 0xEE || b == 0xEF)
                {
                    need = 2; c1lo = 0x80; c1hi = 0xBF; m_fail_code = ErrorCode::None;
                    lead = pos;
                    return;
                }
                if (b == 0xF0)
                {
                    need = 3; c1lo = 0x90; c1hi = 0xBF;
                    m_fail_code = ErrorCode::Utf8Overlong;
                    lead = pos;
                    return;
                }
                if (b == 0xF4)
                {
                    need = 3; c1lo = 0x80; c1hi = 0x8F;
                    m_fail_code = ErrorCode::Utf8ExceedsMax;
                    lead = pos;
                    return;
                }
                if (b >= 0xF1 && b <= 0xF3)
                {
                    need = 3; c1lo = 0x80; c1hi = 0xBF; m_fail_code = ErrorCode::None;
                    lead = pos;
                    return;
                }
                if (b == 0xC0 || b == 0xC1)
                    set(ErrorCode::Utf8Overlong, pos);
                else
                    // 0x80-0xBF without a lead / 0xF5-0xFF
                    set(ErrorCode::Utf8InvalidLead, pos);
                return;
            }
            if (b < 0x20)
                return; // control byte: the pre-existing control check
                       // (string.cpp) reports at this same byte — the
                       // rule table keeps it first
            if (pos == lead + 1)
            {
                // First slot: general range, then the lead's c1 range
                if (b < 0x80 || b > 0xBF)
                    set(ErrorCode::Utf8InvalidContinuation, pos);
                else if (b < c1lo || b > c1hi)
                    set(m_fail_code, lead);
                else
                    --need;
                return;
            }
            if (b < 0x80 || b > 0xBF)
            {
                set(ErrorCode::Utf8InvalidContinuation, pos);
                return;
            }
            --need;
        }

        void Utf8Checker::end()
        {
            if (need != 0 && !failed())
                set(ErrorCode::Utf8Truncated, lead);
        }

        // Phase-1 convenience: feed the whole raw window (pure raw bytes —
        // the fast path guarantees no escape inside).
        ErrorCode check_utf8_strict(const char *p, size_t n, const char *&err_pos)
        {
            Utf8Checker ck;
            ck.feed_range(p, n);
            ck.end();
            err_pos = ck.err;
            return ck.code;
        }

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
         * m_end-bounded JSON5 1.0.0 §5.2 escape / line-continuation decoder
         * (task 40.3). Extends the RFC set (short escapes + \uXXXX with
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
                        ErrorCode ec = check_utf8_strict(
                            start, static_cast<size_t>(m_curr - start), ep);
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
        Utf8Checker ck;
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
     * Parse a JSON5 string with either quote (task 40.1 single quotes,
     * extended by 40.3), scalar path.
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
     * pass through as ordinary raw bytes (40.5 territory; JSON5 permits
     * them).
     */
    bool Parser::parse_string_json5(String &out, char quote)
    {
        // Caller guarantees the leading quote (parse_string check / dispatch).
        ++m_curr;
        const char *start = m_curr;
        char *dst = nullptr;
        Utf8Checker ck;

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
