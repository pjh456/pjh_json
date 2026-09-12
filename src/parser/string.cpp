#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"
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
         */
        struct Utf8Checker
        {
            int need = 0;              // continuation bytes still expected
            int c1lo = 0, c1hi = 0;    // first-slot range (inclusive), valid while need > 0
            const char *lead = nullptr; // sequence start (offset anchor)
            const char *m_fail = nullptr; // first-slot violation message @ lead (else nullptr)
            const char *m_begin;

            explicit Utf8Checker(const char *begin) : m_begin(begin) {}

            void feed(uint8_t b, const char *pos);  // throw on first violation
            void end();                             // throw if a sequence is dangling

            // Feed a raw run in stream order (Phase 2's pre-escape run
            // [start, first escape) needs the same coverage as the loop).
            void feed_range(const char *p, size_t n)
            {
                for (size_t i = 0; i < n; ++i)
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
            if (need == 0)
            {
                if (b <= 0x7F)
                    return; // 0x20-0x7F content (incl. DEL); <0x20 cannot
                            // reach this path (Phase 1 window is clean,
                            // Phase 2 control check throws below)
                if (b >= 0xC2 && b <= 0xDF)
                {
                    need = 1; c1lo = 0x80; c1hi = 0xBF; m_fail = nullptr;
                    lead = pos;
                    return;
                }
                if (b == 0xE0)
                {
                    need = 2; c1lo = 0xA0; c1hi = 0xBF; // 3-byte lead: two continuation slots
                    m_fail = "Overlong UTF-8 sequence in string";
                    lead = pos;
                    return;
                }
                if (b == 0xED)
                {
                    need = 2; c1lo = 0x80; c1hi = 0x9F; // 3-byte lead: two continuation slots
                    m_fail = "UTF-8 surrogate codepoint in string";
                    lead = pos;
                    return;
                }
                if (b >= 0xE1 && b <= 0xEC || b == 0xEE || b == 0xEF)
                {
                    need = 2; c1lo = 0x80; c1hi = 0xBF; m_fail = nullptr;
                    lead = pos;
                    return;
                }
                if (b == 0xF0)
                {
                    need = 3; c1lo = 0x90; c1hi = 0xBF;
                    m_fail = "Overlong UTF-8 sequence in string";
                    lead = pos;
                    return;
                }
                if (b == 0xF4)
                {
                    need = 3; c1lo = 0x80; c1hi = 0x8F;
                    m_fail = "UTF-8 codepoint exceeds U+10FFFF in string";
                    lead = pos;
                    return;
                }
                if (b >= 0xF1 && b <= 0xF3)
                {
                    need = 3; c1lo = 0x80; c1hi = 0xBF; m_fail = nullptr;
                    lead = pos;
                    return;
                }
                if (b == 0xC0 || b == 0xC1)
                    throw_parse_error("Overlong UTF-8 sequence in string", pos, m_begin);
                // 0x80-0xBF without a lead / 0xF5-0xFF
                throw_parse_error("Invalid UTF-8 lead byte in string", pos, m_begin);
            }
            if (b < 0x20)
                return; // control byte: the pre-existing control check
                       // (string.cpp) throws at this same byte — the
                       // rule table keeps it first
            if (pos == lead + 1)
            {
                // First slot: general range, then the lead's c1 range
                if (b < 0x80 || b > 0xBF)
                    throw_parse_error("Invalid UTF-8 continuation byte in string", pos, m_begin);
                if (b < c1lo || b > c1hi)
                    throw_parse_error(m_fail, lead, m_begin);
            }
            else if (b < 0x80 || b > 0xBF)
            {
                throw_parse_error("Invalid UTF-8 continuation byte in string", pos, m_begin);
            }
            --need;
        }

        void Utf8Checker::end()
        {
            if (need != 0)
                throw_parse_error("Truncated UTF-8 sequence in string", lead, m_begin);
        }

        // Phase-1 convenience: feed the whole raw window (pure raw bytes —
        // the fast path guarantees no escape inside).
        void check_utf8_strict(const char *p, size_t n, const char *begin)
        {
            Utf8Checker ck(begin);
            ck.feed_range(p, n);
            ck.end();
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
    String Parser::parse_string()
    {
        if (m_curr >= m_end || *m_curr != '"')
            throw_parse_error("Expected '\"'", m_curr, m_begin);
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
                        check_utf8_strict(start, static_cast<size_t>(m_curr - start), m_begin);
                    std::string_view res(start, m_curr - start);
                    ++m_curr;
                    return res;
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
                        throw_parse_error("Unterminated string", m_curr, m_begin);
                    else
                        throw_parse_error("Unescaped control character in string", m_curr, m_begin);
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
        Utf8Checker ck(m_begin);
        if (m_strict_utf8)
            // Pre-escape raw run [start, first escape): Phase 2's loop
            // starts at the backslash, so cover the run explicitly (it
            // carries no quote/backslash/control — SIMD first-match).
            ck.feed_range(start, static_cast<size_t>(m_curr - start));
        while (true)
        {
            if (*m_curr == '"')
            {
                if (m_strict_utf8)
                    ck.end();
                ++m_curr;
                return std::string_view(start, dst - start);
            }
            if (m_strict_utf8)
                ck.feed(static_cast<uint8_t>(*m_curr), m_curr);
            if (*m_curr == '\\')
            {
                handle_escape(dst, m_curr, m_begin);
            }
            else if (static_cast<uint8_t>(*m_curr) < 0x20)
            {
                throw_parse_error("Unescaped control character in string", m_curr, m_begin);
            }
            else
            {
                *dst++ = *m_curr++;
            }
        }
    }
}
