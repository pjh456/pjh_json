#ifndef INCLUDE_PJH_JSON_GRAMMAR_HPP
#define INCLUDE_PJH_JSON_GRAMMAR_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace pjh::json
{
    /// @brief Shared JSON grammar primitives usable at BOTH compile time
    ///        (validate.hpp / ConstJson) and run time (src/parser).
    ///
    /// Everything here is `constexpr` (NOT `consteval`) and allocation-free,
    /// so a consteval caller and a runtime caller can share the exact same
    /// rule and can no longer drift apart. Header-only and dependency-free
    /// (no xsimd, no pjh_json/detail) so it stays self-contained (task 64).
    ///
    /// Scope: GRAMMAR only. Runtime-only policy -- BOM stripping (task 23),
    /// strict raw UTF-8 (task 24), the finite-double number range (task 61),
    /// the nesting-depth limit (task 05.1) and strict duplicate keys -- is
    /// driven by Config atomics (not constexpr) and std::from_chars, so it
    /// cannot live here. Those remain documented divergences and are pinned
    /// executable by tests/differential_validation.cpp.
    namespace grammar
    {

        // ---- RFC 8259 §2 whitespace: SPACE, TAB, LF, CR only --------------
        /// @return true iff c is one of the four JSON whitespace bytes.
        constexpr bool is_whitespace(unsigned char c) noexcept
        {
            return c == 0x20 || c == 0x09 || c == 0x0A || c == 0x0D;
        }

        // ---- literals -----------------------------------------------------
        inline constexpr std::string_view kTrue = "true";
        inline constexpr std::string_view kFalse = "false";
        inline constexpr std::string_view kNull = "null";

        /// @brief Match `lit` at p and advance p past it on success.
        /// @return false (p untouched) when fewer than lit.size() bytes remain
        ///         or any byte differs.
        constexpr bool match_literal(
            const char *&p, const char *e, std::string_view lit) noexcept
        {
            if (static_cast<std::size_t>(e - p) < lit.size())
                return false;
            for (std::size_t i = 0; i < lit.size(); ++i)
                if (p[i] != lit[i])
                    return false;
            p += lit.size();
            return true;
        }

        // ---- single-character escapes: " \ / b f n r t --------------------
        /// @return the decoded byte for a short escape, or '\0' (which is
        ///         never a valid short-escape result) when c is not one.
        constexpr char short_escape_value(char c) noexcept
        {
            switch (c)
            {
            case '"':  return '"';
            case '\\': return '\\';
            case '/':  return '/';
            case 'b':  return '\b';
            case 'f':  return '\f';
            case 'n':  return '\n';
            case 'r':  return '\r';
            case 't':  return '\t';
            default:   return '\0';
            }
        }

        constexpr bool is_short_escape(char c) noexcept
        {
            return short_escape_value(c) != '\0';
        }

        // ---- hex digits ---------------------------------------------------
        /// @return [0, 15], or -1 when c is not a hex digit.
        constexpr int hex_value(char c) noexcept
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        }

        // ---- UTF-16 surrogate ranges (RFC 8259 §7) ------------------------
        constexpr bool is_high_surrogate(std::uint32_t cp) noexcept
        {
            return cp >= 0xD800u && cp <= 0xDBFFu;
        }
        constexpr bool is_low_surrogate(std::uint32_t cp) noexcept
        {
            return cp >= 0xDC00u && cp <= 0xDFFFu;
        }
        constexpr bool is_surrogate(std::uint32_t cp) noexcept
        {
            return cp >= 0xD800u && cp <= 0xDFFFu;
        }

        // ---- number grammar (RFC 8259 §6) ---------------------------------
        /// @brief Leading-zero rule (RFC 8259 §8.4): '0' is legal only as the
        ///        entire integer part.
        constexpr bool has_leading_zero(
            const char *int_start, std::size_t int_digits) noexcept
        {
            return int_digits > 1 && *int_start == '0';
        }

        /// @brief Structural scan result. GRAMMAR ONLY: the token is accepted
        ///        regardless of magnitude; finite-double range is a
        ///        runtime-only gate (task 61).
        struct number_scan
        {
            const char *int_start = nullptr; ///< first integer digit
            std::size_t int_digits = 0;      ///< digits in the integer part
            bool negative = false;           ///< leading '-' present
            bool is_float = false;           ///< '.' or exponent present
        };

        /// @brief Why scan_number() failed, for the runtime's positioned
        ///        ParseError messages. The cursor p at failure reproduces the
        ///        exact offsets src/parser/number.cpp reported before the
        ///        dedup (see plan 71 §4.5).
        enum class number_error
        {
            ok,
            no_int_digits, ///< '-' with no digit after it
            leading_zero,  ///< more than one integer digit and first is '0'
            no_frac_digits,///< '.' with no digit after it
            no_exp_digits, ///< exponent with no digit after the optional sign
        };

        /// @brief Scan one JSON number's grammar. On success advances p past
        ///        the token and fills out; on failure p is left at the error
        ///        anchor and out is unspecified.
        constexpr number_error scan_number(
            const char *&p, const char *e, number_scan &out) noexcept
        {
            out = number_scan{};

            if (p < e && *p == '-')
            {
                out.negative = true;
                ++p;
            }
            if (p >= e || *p < '0' || *p > '9')
                return number_error::no_int_digits;

            out.int_start = p;
            while (p < e && *p >= '0' && *p <= '9')
                ++p;
            out.int_digits = static_cast<std::size_t>(p - out.int_start);
            if (has_leading_zero(out.int_start, out.int_digits))
                return number_error::leading_zero;

            if (p < e && *p == '.')
            {
                out.is_float = true;
                ++p;
                if (p >= e || *p < '0' || *p > '9')
                    return number_error::no_frac_digits;
                while (p < e && *p >= '0' && *p <= '9')
                    ++p;
            }

            if (p < e && (*p == 'e' || *p == 'E'))
            {
                out.is_float = true;
                ++p;
                if (p < e && (*p == '+' || *p == '-'))
                    ++p;
                if (p >= e || *p < '0' || *p > '9')
                    return number_error::no_exp_digits;
                while (p < e && *p >= '0' && *p <= '9')
                    ++p;
            }
            return number_error::ok;
        }

        // ---- JSON5 1.0.0 grammar additions (task 40.1) --------------------
        // Pure additions: the RFC 8259 rules above are byte-for-byte
        // unchanged. Consumed only when Config::json5() is true, so the
        // default runtime path cannot drift.

        /// @brief JSON5 1.0.0 §8 ASCII white space: the RFC four bytes plus
        ///        VT (0x0B) and FF (0x0C).
        /// @note The default RFC path keeps rejecting VT/FF
        ///       (task 04 strictness); this predicate is opt-in only.
        constexpr bool is_json5_whitespace_ascii(unsigned char c) noexcept
        {
            return is_whitespace(c) || c == 0x0B || c == 0x0C;
        }

        /// @brief JSON5 1.0.0 §8 non-ASCII white space: NBSP (U+00A0), the
        ///        Unicode Zs category (U+1680, U+2000..U+200A, U+202F,
        ///        U+205F, U+3000), the line/paragraph separators U+2028 and
        ///        U+2029, and U+FEFF (BOM used as white space).
        /// @param cp A decoded code point; ASCII (cp <= 0x7F) is not this
        ///        function's job (see is_json5_whitespace).
        /// @note The set is small and fixed, so it stays a table-free
        ///       constexpr predicate. The default RFC path can never reach it.
        constexpr bool is_json5_whitespace_unicode(std::uint32_t cp) noexcept
        {
            return cp == 0x00A0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 || cp == 0x2029 ||
                   cp == 0x202F || cp == 0x205F || cp == 0x3000 || cp == 0xFEFF;
        }

        /// @brief JSON5 1.0.0 §8 white space for any decoded code point:
        ///        the ASCII set plus the non-ASCII set above.
        constexpr bool is_json5_whitespace(std::uint32_t cp) noexcept
        {
            return cp <= 0x7F ? is_json5_whitespace_ascii(static_cast<unsigned char>(cp))
                              : is_json5_whitespace_unicode(cp);
        }

        /// @brief JSON5 1.0.0 §3 IdentifierName, ASCII subset: a key may
        ///        start with an ASCII letter, '_' or '$'.
        /// @note The Unicode IdentifierName closure (and `\uXXXX` escapes)
        ///       landed in task 40.5 as the runtime-only `unicode::`
        ///       helpers in src/parser/unicode.hpp (they need generated
        ///       Unicode category tables and are never used at compile
        ///       time); this ASCII predicate stays the shared source for
        ///       the common case.
        constexpr bool is_identifier_start(unsigned char c) noexcept
        {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '$';
        }

        /// @brief JSON5 1.0.0 §3 IdentifierName continuation, ASCII subset:
        ///        start characters plus ASCII digits.
        constexpr bool is_identifier_continue(unsigned char c) noexcept
        {
            return is_identifier_start(c) || (c >= '0' && c <= '9');
        }

        // ---- JSON5 1.0.0 §6 number grammar (task 40.2) --------------------
        // A JSON5-specific scanner alongside the RFC scan_number above; the
        // RFC function is byte-for-byte untouched and stays the only rule
        // consumed on the default path. This one adds the JSON5 spellings:
        // a leading '+' sign, hexadecimal integers (0x/0X), and the ES5.1
        // decimal forms with an omitted integer part (`.5`) or an omitted
        // fraction part (`5.`, `5.e3`). It reuses the same `number_error`
        // vocabulary (no new ErrorCode) and `has_leading_zero` so the two
        // scanners cannot disagree on the shared leading-zero rule.
        // `Infinity`/`NaN` are NOT handled here (deferred to task 40.4).
        struct number_scan_json5
        {
            const char *int_start = nullptr; ///< first digit (decimal or hex)
            std::size_t int_digits = 0;      ///< digits in that run
            bool negative = false;           ///< leading '-' present
            bool explicit_plus = false;      ///< leading '+' present
            bool is_float = false;           ///< '.' or exponent present
            bool is_hex = false;             ///< `0x`/`0X` hexadecimal integer
        };

        /// @brief Scan one JSON5 number's grammar. On success advances p past
        ///        the token and fills out; on failure p is left at the error
        ///        anchor (same convention as scan_number).
        /// @note Hex has no fraction/exponent: after a valid `0x` digit run
        ///       the token ends, matching the ES5.1 HexIntegerLiteral rule.
        constexpr number_error scan_number_json5(const char *&p, const char *e, number_scan_json5 &out) noexcept
        {
            out = number_scan_json5{};

            if (p < e && (*p == '+' || *p == '-'))
            {
                out.negative = (*p == '-');
                out.explicit_plus = (*p == '+');
                ++p;
            }

            // HexIntegerLiteral: 0x/0X then one or more hex digits.
            if (p + 1 < e && p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
            {
                out.is_hex = true;
                p += 2;
                out.int_start = p;
                while (p < e && hex_value(*p) >= 0)
                    ++p;
                out.int_digits = static_cast<std::size_t>(p - out.int_start);
                if (out.int_digits == 0)
                    return number_error::no_int_digits;
                return number_error::ok;
            }

            // DecimalLiteral, allowing the ES5.1 optional-digit fraction:
            //   .5   5.   5.e3   .5e3
            if (p < e && *p == '.')
            {
                out.is_float = true;
                ++p;
                if (p >= e || *p < '0' || *p > '9')
                    return number_error::no_frac_digits;
                out.int_start = p;
                while (p < e && *p >= '0' && *p <= '9')
                    ++p;
                out.int_digits = static_cast<std::size_t>(p - out.int_start);
            }
            else
            {
                if (p >= e || *p < '0' || *p > '9')
                    return number_error::no_int_digits;
                out.int_start = p;
                while (p < e && *p >= '0' && *p <= '9')
                    ++p;
                out.int_digits = static_cast<std::size_t>(p - out.int_start);
                if (has_leading_zero(out.int_start, out.int_digits))
                    return number_error::leading_zero;
                if (p < e && *p == '.')
                {
                    out.is_float = true;
                    ++p;
                    while (p < e && *p >= '0' && *p <= '9')
                        ++p;
                }
            }

            if (p < e && (*p == 'e' || *p == 'E'))
            {
                out.is_float = true;
                ++p;
                if (p < e && (*p == '+' || *p == '-'))
                    ++p;
                if (p >= e || *p < '0' || *p > '9')
                    return number_error::no_exp_digits;
                while (p < e && *p >= '0' && *p <= '9')
                    ++p;
            }
            return number_error::ok;
        }

        // ---- JSON5 1.0.0 §6 non-finite literals (task 40.4) ---------------
        // `Infinity` and `NaN` are NumericLiterals in JSON5 1.0.0, each
        // optionally preceded by `+` or `-` (the sign is consumed by the
        // runtime caller, which owns the value mapping). The RFC scanner
        // above keeps rejecting both spellings byte for byte.

        /// @brief JSON5 non-finite numeric literal spellings.
        enum class json5_nonfinite
        {
            none,     ///< not a non-finite literal
            infinity, ///< `Infinity`
            nan       ///< `NaN`
        };

        /// @brief Match `Infinity` or `NaN` at @p p (sign already consumed).
        /// @param p Cursor; advanced past the spelling on success, untouched
        ///        on failure.
        /// @param e End of the input range (`m_end`); matching is bounded by
        ///        it, never by NUL padding.
        /// @param out Receives the matched spelling; `none` on failure.
        /// @return true iff the exact, case-sensitive spelling matched.
        constexpr bool match_json5_nonfinite(const char *&p, const char *e, json5_nonfinite &out) noexcept
        {
            out = json5_nonfinite::none;
            if (match_literal(p, e, "Infinity"))
            {
                out = json5_nonfinite::infinity;
                return true;
            }
            if (match_literal(p, e, "NaN"))
            {
                out = json5_nonfinite::nan;
                return true;
            }
            return false;
        }

        // ---- JSON5 1.0.0 §5.2 string escapes (task 40.3) ------------------
        // Pure additions alongside short_escape_value: the RFC helper is
        // byte-for-byte unchanged. JSON5 adds the `\'` and `\v` single
        // escapes. `\0` (NUL) deliberately has no entry here: a char return
        // would collide with the not-an-escape sentinel, so the runtime
        // decoder special-cases it (including the "not followed by a decimal
        // digit" lookahead). `\xHH`, identity escapes and line continuations
        // are likewise runtime concerns.

        /// @brief JSON5 1.0.0 §5.2 single-character escapes: the RFC set
        ///        plus `\'` -> `'` and `\v` -> VT (0x0B).
        /// @return the decoded byte, or '\0' (never a valid result here)
        ///         when c is not a single escape.
        constexpr char short_escape_value_json5(char c) noexcept
        {
            switch (c)
            {
            case '\'':
                return '\'';
            case 'v':
                return static_cast<char>(0x0B);
            default:
                return short_escape_value(c);
            }
        }

        /// @brief JSON5 1.0.0 LineTerminator, ASCII subset: LF or CR.
        /// @note U+2028/U+2029 are the Unicode members of the
        ///       LineTerminator set; being multi-byte UTF-8 sequences they
        ///       are matched by the runtime scanner, not by this byte
        ///       predicate.
        constexpr bool is_json5_line_terminator_ascii(unsigned char c) noexcept
        {
            return c == 0x0A || c == 0x0D;
        }

    } // namespace grammar

} // namespace pjh::json

#endif // INCLUDE_PJH_JSON_GRAMMAR_HPP
