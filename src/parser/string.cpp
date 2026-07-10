#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"
#include "internal.hpp"
#include <xsimd/xsimd.hpp>

namespace pjh::json
{
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
            throw ParseError("Expected '\"'");
        ++m_curr;

        const char *start = m_curr;
        char *dst = nullptr;

        using batch_type = xsimd::batch<uint8_t>;
        std::size_t batch_size = batch_type::size;
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
                        throw ParseError("Unterminated string");
                    else
                        throw ParseError("Unescaped control character in string");
                }
            }
            else
                m_curr += batch_size;
        }

    // Phase 2: scalar fallback — decode escapes in-place
    // (safe: escaped forms expand to fewer bytes than source)
    insitu_fallback:
        while (true)
        {
            if (*m_curr == '"')
            {
                ++m_curr;
                return std::string_view(start, dst - start);
            }
            else if (*m_curr == '\\')
            {
                handle_escape(dst, m_curr, *this);
            }
            else if (static_cast<uint8_t>(*m_curr) < 0x20)
            {
                throw ParseError("Unescaped control character in string");
            }
            else
            {
                *dst++ = *m_curr++;
            }
        }
    }
}
