#ifndef INCLUDE_PJH_JSON_DETAIL_UTF8_HPP
#define INCLUDE_PJH_JSON_DETAIL_UTF8_HPP

#include "pjh_json/error.hpp"

#include <cstddef>
#include <cstdint>

namespace pjh::json
{
    namespace detail
    {
        /*
         * Strict UTF-8 gate (opt-in: Config::strict_utf8). Sequential scalar
         * state machine over RAW string content only:
         *   Phase 1 (no escapes): check_utf8_strict over [open+1, close)
         *     (the DOM fast path guarantees no escape inside the window).
         *   Phase 2 (escapes): every byte is fed at its ORIGINAL source
         *     position in stream order; the backslash of an escape is fed
         *     (a torn sequence gets a bad continuation at the backslash),
         *     its ASCII source bytes are skipped with the escape, and the
         *     decoded output is never fed (well-formed by construction).
         * Rule set mirrors the writer's ascii-mode decoder
         * (src/writer/string.cpp write_escaped_ascii); the six message
         * classes are grep-identical on both sides. Offsets: first
         * offending byte - the bad byte itself for bad continuations, the
         * sequence lead byte for overlong / surrogate / >U+10FFFF /
         * truncated. All byte tests are uint8_t (0xE0 is negative as signed
         * char on two's-complement hosts).
         *
         * The checker never throws: it records the first failure
         * (ErrorCode + offending byte) and becomes a no-op afterwards
         * (first error wins). Shared by Parser::parse_string and the
         * StreamReader event core so the two cannot drift (task 35.3).
         */
        struct Utf8Checker
        {
            int need = 0;                            // continuation bytes still expected
            int c1lo = 0, c1hi = 0;                  // first-slot range (inclusive), valid while need > 0
            const char *lead = nullptr;              // sequence start (offset anchor)
            ErrorCode m_fail_code = ErrorCode::None; // first-slot violation @ lead (else None)
            ErrorCode code = ErrorCode::None;        // first failure wins
            const char *err = nullptr;               // failure position

            [[nodiscard]] bool failed() const noexcept { return code != ErrorCode::None; }

            void set(ErrorCode c, const char *pos) noexcept
            {
                if (!failed())
                {
                    code = c;
                    err = pos;
                }
            }

            void feed(uint8_t b, const char *pos); // records on first violation
            void end();                            // records a dangling sequence

            // Feed a raw run in stream order. A run stops at the first
            // recorded failure.
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
        inline void Utf8Checker::feed(uint8_t b, const char *pos)
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
                    need = 1;
                    c1lo = 0x80;
                    c1hi = 0xBF;
                    m_fail_code = ErrorCode::None;
                    lead = pos;
                    return;
                }
                if (b == 0xE0)
                {
                    need = 2;
                    c1lo = 0xA0;
                    c1hi = 0xBF; // 3-byte lead: two continuation slots
                    m_fail_code = ErrorCode::Utf8Overlong;
                    lead = pos;
                    return;
                }
                if (b == 0xED)
                {
                    need = 2;
                    c1lo = 0x80;
                    c1hi = 0x9F; // 3-byte lead: two continuation slots
                    m_fail_code = ErrorCode::Utf8Surrogate;
                    lead = pos;
                    return;
                }
                if ((b >= 0xE1 && b <= 0xEC) || b == 0xEE || b == 0xEF)
                {
                    need = 2;
                    c1lo = 0x80;
                    c1hi = 0xBF;
                    m_fail_code = ErrorCode::None;
                    lead = pos;
                    return;
                }
                if (b == 0xF0)
                {
                    need = 3;
                    c1lo = 0x90;
                    c1hi = 0xBF;
                    m_fail_code = ErrorCode::Utf8Overlong;
                    lead = pos;
                    return;
                }
                if (b == 0xF4)
                {
                    need = 3;
                    c1lo = 0x80;
                    c1hi = 0x8F;
                    m_fail_code = ErrorCode::Utf8ExceedsMax;
                    lead = pos;
                    return;
                }
                if (b >= 0xF1 && b <= 0xF3)
                {
                    need = 3;
                    c1lo = 0x80;
                    c1hi = 0xBF;
                    m_fail_code = ErrorCode::None;
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
                        // reports at this same byte - the rule table keeps
                        // it first
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

        inline void Utf8Checker::end()
        {
            if (need != 0 && !failed())
                set(ErrorCode::Utf8Truncated, lead);
        }

        // Phase-1 convenience: feed the whole raw window (pure raw bytes -
        // the caller guarantees no escape inside).
        [[nodiscard]] inline ErrorCode check_utf8_strict(const char *p, size_t n, const char *&err_pos)
        {
            Utf8Checker ck;
            ck.feed_range(p, n);
            ck.end();
            err_pos = ck.err;
            return ck.code;
        }

    } // namespace detail

} // namespace pjh::json

#endif // INCLUDE_PJH_JSON_DETAIL_UTF8_HPP
