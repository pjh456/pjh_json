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
     * Classify a grammar-scanned RFC 8259 number token (shared by the DOM
     * parser and the streaming event core; declaration in detail/utils.hpp).
     *
     * 1. Pure-integer token: int64 when the magnitude fits, else double.
     *    - <= 18 digits always fit (max 999999999999999999 < INT64_MAX).
     *    - 19 digits are compared digit by digit against the INT64 limits
     *      (see fits_int64_19); the negative magnitude limit is one above
     *      the positive limit because |INT64_MIN| = 2^63.
     *    - >= 20 digits never fit int64 (the value model has no unsigned
     *      slot) and fall to double, rounded to the nearest representable
     *      value (e.g. UINT64_MAX -> 2^64.0). That rounding is a property
     *      of double, not a parse error.
     * 2. Float indicators, or a 19+-digit integer out of int64 range:
     *    parse the whole token as double via std::from_chars. When its
     *    error code reports out-of-range, the token is grammar-legal but
     *    its magnitude is outside the finite-double range (the documented
     *    RFC 8259 §6 limit): that is a range error, not a format error.
     *    The output value is unspecified on out-of-range across standard
     *    libraries, so only the error code may be inspected.
     */
    ErrorCode classify_number(const char *token_begin, const char *token_end, const grammar::number_scan &scan,
                              Json &out, const char *&err_pos) noexcept
    {
        const bool is_negative = scan.negative;
        const char *const int_start = scan.int_start;
        const uint32_t digits = static_cast<uint32_t>(scan.int_digits);

        // Pure integer token: int64 when the magnitude fits, else double
        if (!scan.is_float)
        {
            // <= 18 digits always fit int64 (max 999999999999999999 < INT64_MAX)
            if (digits < 19)
            {
                uint64_t uval = parse_u64(int_start, digits);
                int64_t val = is_negative ? -static_cast<int64_t>(uval) : static_cast<int64_t>(uval);
                out = Json(val);
                return ErrorCode::None;
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
                out = Json(val);
                return ErrorCode::None;
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
        auto [end, ec] = std::from_chars(token_begin, token_end, val);
        if (ec == std::errc::result_out_of_range)
        {
            err_pos = token_begin; // token start, including a leading '-'
            return ErrorCode::NumberOutOfRange;
        }
        if (ec != std::errc{} || end != token_end)
        {
            err_pos = token_end;
            return ErrorCode::NumberInvalidFormat;
        }
        out = Json(val);
        return ErrorCode::None;
    }

    /*
     * Parse JSON number
     *
     * 1. Grammar scan (shared with the compile-time validator, grammar.hpp);
     *    the error anchors reproduce the pre-dedup cursors byte for byte.
     * 2. classify_number decides int64 vs double and reports range/format
     *    failures with the same anchors the parser always used.
     */
    bool Parser::parse_number(Json &out)
    {
        // JSON5 number extensions: hex / leading '+' / omitted
        // dot side. Kept behind the captured knob so the RFC body below stays
        // byte-for-byte identical on the default path.
        if (m_json5)
            return parse_number_json5(out);

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
            fail(ErrorCode::NumberNoIntDigits, m_curr);
            return false;
        case grammar::number_error::leading_zero:
            // cursor past the integer run, exactly as before
            fail(ErrorCode::NumberLeadingZero, m_curr);
            return false;
        case grammar::number_error::no_frac_digits:
            // cursor right after '.', exactly as before
            fail(ErrorCode::NumberNoFracDigits, m_curr);
            return false;
        case grammar::number_error::no_exp_digits:
            // cursor right after the optional exponent sign, exactly as before
            fail(ErrorCode::NumberNoExpDigits, m_curr);
            return false;
        }

        Json val;
        const char *err_pos = nullptr;
        const ErrorCode ec = classify_number(start, m_curr, scan, val, err_pos);
        if (ec != ErrorCode::None)
        {
            fail(ec, err_pos);
            return false;
        }
        out = std::move(val);
        return true;
    }

    /*
     * Parse JSON5 number
     *
     * 1. grammar::scan_number_json5 validates the JSON5 spellings (leading
     *    '+' or '-', hex 0x/0X, leading/trailing decimal point) and reports
     *    the same number_error vocabulary as the RFC scanner; the error
     *    anchors reproduce that scanner's cursor convention, so reuse the
     *    existing ErrorCodes (no new ErrorCode, golden table untouched).
     * 2. Hex: values with <= 16 significant hex digits are accumulated
     *    exactly and stay Integer whenever they fit int64 (INT64_MIN's
     *    2^63 magnitude special-cased); anything larger falls to double via
     *    std::from_chars(chars_format::hex) over the bare digit run (the
     *    prefix-less form the standard parses), which also reports
     *    result_out_of_range for a magnitude beyond the finite-double range.
     * 3. Decimal: the RFC int64 path is reused verbatim for pure integers
     *    (sign included), and the double fallback feeds from_chars the token
     *    without an explicit JSON5 '+', which the standard does not accept.
     *    from_chars general already understands `.5`, `5.` and `5.e3`.
     * 4. `Infinity`/`NaN`, each with an optional `+`/`-`, are
     *    tested first and produce the std::numeric_limits constants. This is
     *    a pure bit/materialisation path: no from_chars, no isfinite and no
     *    FP comparison, so -ffast-math cannot change which spellings are
     *    accepted (contrast the writer, which needs isfinite and is
     *    documented as fast-math sensitive).
     */
    bool Parser::parse_number_json5(Json &out)
    {
        const char *const start = m_curr;

        // JSON5 1.0.0 §6: Infinity | NaN with an optional leading sign. The
        // check precedes the numeric scanner, whose decimal path would
        // otherwise stop on the 'I'/'N' first byte and report no_int_digits.
        {
            const char *p = m_curr;
            bool negative = false;
            if (p < m_end && (*p == '+' || *p == '-'))
            {
                negative = (*p == '-');
                ++p;
            }
            grammar::json5_nonfinite nf = grammar::json5_nonfinite::none;
            if (grammar::match_json5_nonfinite(p, m_end, nf))
            {
                m_curr = p;
                double d = (nf == grammar::json5_nonfinite::infinity) ? std::numeric_limits<double>::infinity()
                                                                      : std::numeric_limits<double>::quiet_NaN();
                // `-NaN` is still NaN (the sign sets only the sign bit);
                // `-Infinity` is negative infinity.
                out = Json(negative ? -d : d);
                return true;
            }
        }

        grammar::number_scan_json5 scan;
        switch (grammar::scan_number_json5(m_curr, m_end, scan))
        {
        case grammar::number_error::ok:
            break;
        case grammar::number_error::no_int_digits:
            // Also anchor for `+`/`-` with no number and `0x` with no digit.
            fail(ErrorCode::NumberNoIntDigits, m_curr);
            return false;
        case grammar::number_error::leading_zero:
            fail(ErrorCode::NumberLeadingZero, m_curr);
            return false;
        case grammar::number_error::no_frac_digits:
            fail(ErrorCode::NumberNoFracDigits, m_curr);
            return false;
        case grammar::number_error::no_exp_digits:
            fail(ErrorCode::NumberNoExpDigits, m_curr);
            return false;
        }

        const bool is_negative = scan.negative;
        const char *const int_start = scan.int_start;
        const uint32_t digits = static_cast<uint32_t>(scan.int_digits);

        if (scan.is_hex)
        {
            // Skip leading zeros before sizing the magnitude: `0x0001` is a
            // one-significant-digit int, not a 17-bit overflow.
            const char *sig = int_start;
            while (sig < m_curr && *sig == '0')
                ++sig;
            const size_t sig_digits = static_cast<size_t>(m_curr - sig);

            if (sig_digits <= 16)
            {
                // 16 significant hex digits are the largest that can fit in
                // uint64; accumulate the whole run (leading zeros do not
                // raise the value).
                uint64_t uval = 0;
                for (const char *q = int_start; q < m_curr; ++q)
                    uval = uval * 16u + static_cast<uint64_t>(grammar::hex_value(*q));

                bool fits = true;
                if (sig_digits == 16)
                {
                    // INT64_MAX = 0x7FFF...; |INT64_MIN| = 0x8000...
                    const uint64_t pos_limit = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
                    fits = is_negative ? (uval <= kInt64MinAbs) : (uval <= pos_limit);
                }
                if (fits)
                {
                    int64_t val;
                    if (is_negative && uval == kInt64MinAbs)
                        val = std::numeric_limits<int64_t>::min();
                    else
                        val = is_negative ? -static_cast<int64_t>(uval) : static_cast<int64_t>(uval);
                    out = Json(val);
                    return true;
                }
            }

            // Too large for int64 (or outside its signed range): double. The
            // hex formatter reads a bare hex-digit run and yields
            // result_out_of_range on overflow, independent of -ffast-math.
            double dval = 0.0;
            auto [end, ec] = std::from_chars(sig, m_curr, dval, std::chars_format::hex);
            if (ec == std::errc::result_out_of_range)
            {
                fail(ErrorCode::NumberOutOfRange, start);
                return false;
            }
            if (ec != std::errc{} || end != m_curr)
            {
                fail(ErrorCode::NumberInvalidFormat, m_curr);
                return false;
            }
            out = Json(is_negative ? -dval : dval);
            return true;
        }

        // Pure decimal integer: same int64 gate as the RFC path.
        if (!scan.is_float)
        {
            if (digits < 19)
            {
                uint64_t uval = parse_u64(int_start, digits);
                int64_t val = is_negative ? -static_cast<int64_t>(uval) : static_cast<int64_t>(uval);
                out = Json(val);
                return true;
            }
            if (digits == 19 && fits_int64_19(int_start, is_negative))
            {
                uint64_t uval = parse_u64(int_start, digits);
                int64_t val;
                if (is_negative && uval == kInt64MinAbs)
                    val = std::numeric_limits<int64_t>::min();
                else
                    val = is_negative ? -static_cast<int64_t>(uval) : static_cast<int64_t>(uval);
                out = Json(val);
                return true;
            }
        }

        // from_chars does not accept an explicit JSON5 '+', so feed it the
        // spelling after the sign; the anchor for a range error stays the
        // token start (including the sign), matching the RFC path.
        const char *const num_start = scan.explicit_plus ? start + 1 : start;
        double val = 0.0;
        auto [end, ec] = std::from_chars(num_start, m_curr, val);
        if (ec == std::errc::result_out_of_range)
        {
            fail(ErrorCode::NumberOutOfRange, start);
            return false;
        }
        if (ec != std::errc{} || end != m_curr)
        {
            fail(ErrorCode::NumberInvalidFormat, m_curr);
            return false;
        }
        out = Json(val);
        return true;
    }
}
