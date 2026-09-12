#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"
#include "pjh_json/detail/utils.hpp"
#include "pjh_json/grammar.hpp"
#include <charconv>
#include <limits>

namespace pjh::json
{
    /*
     * Parse n-digit unsigned integer (digits already validated, no overflow
     * check: callers bound n to <= 19, and max 9999999999999999999 < 2^64-1,
     * so uint64 holds every reachable input).
     */
    static uint64_t parse_u64(const char *p, uint32_t n)
    {
        uint64_t v = 0;
        for (uint32_t i = 0; i < n; ++i)
            v = v * 10 + (p[i] - '0');
        return v;
    }

    static constexpr uint64_t kInt64MinAbs = 9223372036854775808ULL; // 2^63
    static constexpr std::string_view kInt64MaxDigits = "9223372036854775807";
    static constexpr std::string_view kInt64MinAbsDigits = "9223372036854775808";

    /*
     * 19-digit integer part (digits already validated, no leading zeros,
     * caller guarantees exactly 19): equal-width lexicographic comparison
     * is numeric comparison. Positive limit is INT64_MAX; the negative
     * magnitude limit is 2^63 (INT64_MIN has no positive twin).
     */
    static bool fits_int64_19(const char *p, bool negative)
    {
        const std::string_view limit = negative ? kInt64MinAbsDigits : kInt64MaxDigits;
        return std::string_view(p, 19) <= limit;
    }

    /*
     * Parse JSON number
     *
     * 1. Consume optional leading '-'.
     * 2. Consume integer digits; reject no-digits and leading-zero errors.
     * 3. Consume fractional / exponent parts if present (float indicators).
     * 4. Pure-integer token: int64 when the magnitude fits, else double.
     *    - <= 18 digits always fit (max 999999999999999999 < INT64_MAX).
     *    - 19 digits are compared digit by digit against the INT64 limits
     *      (see fits_int64_19); the negative magnitude limit is one above
     *      the positive limit because |INT64_MIN| = 2^63.
     *    - >= 20 digits never fit int64 (the value model has no unsigned
     *      slot) and fall to double, rounded to the nearest representable
     *      value (e.g. UINT64_MAX -> 2^64.0). That rounding is a property
     *      of double, not a parse error.
     * 5. Float indicators, or a 19+-digit integer out of int64 range:
     *    parse the whole token as double via std::from_chars. When its
     *    error code reports out-of-range, the token is grammar-legal but
     *    its magnitude is outside the finite-double range (the documented
     *    RFC 8259 §6 limit): that is a range error, not a format error.
     *    The output value is unspecified on out-of-range across standard
     *    libraries, so only the error code may be inspected.
     */
    Json Parser::parse_number()
    {
        const char *start = m_curr;

        // Grammar scan (shared with the compile-time validator, grammar.hpp).
        // The error anchors reproduce the pre-dedup cursors byte for byte.
        grammar::number_scan scan;
        switch (grammar::scan_number(m_curr, m_end, scan))
        {
        case grammar::number_error::ok:
            break;
        case grammar::number_error::no_int_digits:
            // cursor at the first non-digit (== after '-' when signed)
            throw_parse_error("Invalid number: no digits after '-'", m_curr, m_begin);
        case grammar::number_error::leading_zero:
            // cursor past the integer run, exactly as before
            throw_parse_error("Invalid number: leading zeros are not allowed", m_curr, m_begin);
        case grammar::number_error::no_frac_digits:
            // cursor right after '.', exactly as before
            throw_parse_error("Invalid number: no digits after decimal point", m_curr, m_begin);
        case grammar::number_error::no_exp_digits:
            // cursor right after the optional exponent sign, exactly as before
            throw_parse_error("Invalid number: no digits in exponent", m_curr, m_begin);
        }

        const bool is_negative = scan.negative;
        const char *const int_start = scan.int_start;
        const uint32_t digits = static_cast<uint32_t>(scan.int_digits);
        const bool is_float = scan.is_float;

        // Pure integer token: int64 when the magnitude fits, else double
        if (!is_float)
        {
            // <= 18 digits always fit int64 (max 999999999999999999 < INT64_MAX)
            if (digits < 19)
            {
                uint64_t uval = parse_u64(int_start, digits);
                int64_t val = is_negative ? -static_cast<int64_t>(uval) : static_cast<int64_t>(uval);
                return Json(val);
            }
            // 19 digits: digit-by-digit comparison against the INT64 limits
            if (digits == 19 && fits_int64_19(int_start, is_negative))
            {
                uint64_t uval = parse_u64(int_start, digits);
                int64_t val;
                if (is_negative && uval == kInt64MinAbs)
                    val = std::numeric_limits<int64_t>::min(); // -2^63: negation would overflow
                else
                    val = is_negative ? -static_cast<int64_t>(uval) : static_cast<int64_t>(uval);
                return Json(val);
            }
        }

        // Float indicators, or integer out of int64 range: parse as double.
        // RFC 8259 §6 permits implementations to bound the accepted number
        // range; this library's bound is a finite IEEE-754 double. A token
        // that matches the grammar but is not representable as a finite
        // double (overflow to infinity, or underflow to zero) is a RANGE
        // error, not a format error — report it distinctly and do not read
        // the output value: when the result is out-of-range its value is
        // unspecified across implementations (libstdc++ leaves it
        // unmodified; libc++/MSVC write +/-inf or +/-0), and the error code
        // does not tell overflow from underflow (LWG 3081).
        double val = 0.0;
        auto [end, ec] = std::from_chars(start, m_curr, val);
        if (ec == std::errc::result_out_of_range)
            throw_parse_error("Number out of double range", start, m_begin);
        if (ec != std::errc{} || end != m_curr)
            throw_parse_error("Invalid number format", m_curr, m_begin);
        return Json(val);
    }
}
