#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"
#include <charconv>

namespace pjh::json
{
    /*
     * Parse n-digit unsigned integer (digits already validated, no overflow check).
     */
    static uint64_t parse_u64(const char *p, uint32_t n)
    {
        uint64_t v = 0;
        for (uint32_t i = 0; i < n; ++i)
            v = v * 10 + (p[i] - '0');
        return v;
    }

    /*
     * Parse JSON number
     *
     * 1. Consume optional leading '-'.
     * 2. Consume integer digits; reject no-digits and leading-zero errors.
     * 3. If decimal point, exponent, or digits > 18 (uint64 overflow threshold),
     *    parse as double via std::from_chars.
     * 4. Otherwise, parse as int64 via parse_u64 and apply sign.
     */
    Json Parser::parse_number()
    {
        const char *start = m_curr;
        bool is_negative = false;

        // Optional sign
        if (*m_curr == '-')
        {
            is_negative = true;
            ++m_curr;
        }

        // Integer part
        const char *int_start = m_curr;
        uint32_t digits = 0;

        while (*m_curr >= '0' && *m_curr <= '9')
        {
            ++m_curr;
            ++digits;
        }

        if (digits == 0)
            throw_error("Invalid number: no digits after '-'");
        if (digits > 1 && *int_start == '0')
            throw_error("Invalid number: leading zeros are not allowed");

        // Check for float indicators: '.', 'e'/'E', or overflow (>18 digits)
        if (*m_curr == '.' || *m_curr == 'e' || *m_curr == 'E' || digits > 18)
        {
            // Fractional part
            if (*m_curr == '.')
            {
                ++m_curr;
                uint32_t frac = 0;
                while (*m_curr >= '0' && *m_curr <= '9')
                {
                    ++m_curr;
                    ++frac;
                }
                if (frac == 0)
                    throw_error("Invalid number: no digits after decimal point");
            }
            // Exponent part
            if (*m_curr == 'e' || *m_curr == 'E')
            {
                ++m_curr;
                if (*m_curr == '+' || *m_curr == '-')
                    ++m_curr;
                uint32_t exp = 0;
                while (*m_curr >= '0' && *m_curr <= '9')
                {
                    ++m_curr;
                    ++exp;
                }
                if (exp == 0)
                    throw_error("Invalid number: no digits in exponent");
            }
            // Parse entire token as double
            double val = 0.0;
            auto [end, ec] = std::from_chars(start, m_curr, val);
            if (ec != std::errc{} || end != m_curr)
                throw_error("Invalid number format");
            return Json(val);
        }

        // No float indicators, parse as int64
        uint64_t uval = parse_u64(int_start, digits);
        int64_t val = is_negative ? -static_cast<int64_t>(uval) : static_cast<int64_t>(uval);
        return Json(val);
    }
}
