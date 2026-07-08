#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"
#include "internal.hpp"
#include <xsimd/xsimd.hpp>

namespace pjh::json
{
    std::string_view Parser::parse_string()
    {
        if (m_curr >= m_end || *m_curr != '"')
            error("Expected '\"'");
        ++m_curr;

        const char *start = m_curr;
        char *dst = nullptr;

        using batch_type = xsimd::batch<uint8_t>;
        std::size_t batch_size = batch_type::size;
        auto quote = xsimd::broadcast<uint8_t>('"');
        auto escape = xsimd::broadcast<uint8_t>('\\');
        auto ctrl = xsimd::broadcast<uint8_t>(0x20);

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

                if (*m_curr == '"')
                {
                    std::string_view res(start, m_curr - start);
                    ++m_curr;
                    return res;
                }
                else if (*m_curr == '\\')
                {
                    dst = const_cast<char *>(start) + (m_curr - start);
                    goto insitu_fallback;
                }
                else
                {
                    if (m_curr >= m_end)
                        error("Unterminated string");
                    else
                        error("Unescaped control character in string");
                }
            }
            else
                m_curr += batch_size;
        }

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
                handle_escape(dst, m_curr, this);
            }
            else if (static_cast<uint8_t>(*m_curr) < 0x20)
            {
                error("Unescaped control character in string");
            }
            else
            {
                *dst++ = *m_curr++;
            }
        }
    }
}
