#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"
#include <charconv>

namespace pjh::json
{
    Json Parser::parse_number()
    {
        const char *start = m_curr;
        bool is_negative = false;

        if (*m_curr == '-')
        {
            is_negative = true;
            ++m_curr;
        }

        uint64_t uval = 0;
        uint32_t digits = 0;

        while (*m_curr >= '0' && *m_curr <= '9')
        {
            uval = uval * 10 + (*m_curr - '0');
            ++m_curr;
            ++digits;
        }

        if (digits == 0)
            error("Invalid number: no digits after '-'");
        if (digits > 1 && (is_negative ? start[1] : start[0]) == '0')
            error("Invalid number: leading zeros are not allowed");

        if (*m_curr == '.' || *m_curr == 'e' || *m_curr == 'E' || digits > 18)
        {
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
                    error("Invalid number: no digits after decimal point");
            }
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
                    error("Invalid number: no digits in exponent");
            }
            double val = 0.0;
            auto [end, ec] = std::from_chars(start, m_curr, val);
            if (ec != std::errc{} || end != m_curr)
                error("Invalid number format");
            return make_float(val);
        }

        int64_t val = is_negative ? -static_cast<int64_t>(uval) : static_cast<int64_t>(uval);
        return make_int(val);
    }
}
