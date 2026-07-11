#ifndef INCLUDE_PJH_JSON_VALIDATE_HPP
#define INCLUDE_PJH_JSON_VALIDATE_HPP

#include <string_view>

namespace pjh::json
{

    /// @brief Compile-time JSON validator.
    ///
    /// Walks a JSON source string at compile time and returns whether it is
    /// syntactically valid JSON.  Completely independent of any JSON type
    /// system — only cares about the grammar, not about building a DOM.
    namespace validate
    {

        // Forward declarations for mutual recursion between value(), array(),
        // and object().
        consteval bool value(const char *&p, const char *e);
        consteval bool array(const char *&p, const char *e);
        consteval bool object(const char *&p, const char *e);

        /// @brief Skip JSON whitespace characters (space, tab, newline, carriage return).
        /// @param p Reference to the current parse position; advanced past whitespace.
        /// @param e Pointer to the end of the input buffer.
        consteval void skip_whitespace(const char *&p, const char *e)
        {
            while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
                ++p;
        }

        /// @brief Check whether the current position matches a literal string,
        ///        and if so, advance past it.
        /// @param p Reference to the current parse position; advanced on match.
        /// @param e Pointer to the end of the input buffer.
        /// @param lit The literal string to match (e.g. "true", "false", "null").
        /// @param len Length of the literal string in bytes.
        /// @return true if the literal was matched and p was advanced.
        consteval bool match_literal(
            const char *&p, const char *e,
            const char *lit, size_t len)
        {
            if (static_cast<size_t>(e - p) < len)
                return false;
            for (size_t i = 0; i < len; ++i)
                if (p[i] != lit[i])
                    return false;
            p += len;
            return true;
        }

        /// @brief Skip a consecutive run of ASCII decimal digits ('0'–'9').
        /// @param p Reference to the current parse position; advanced past digits.
        /// @param e Pointer to the end of the input buffer.
        consteval void skip_digits(const char *&p, const char *e)
        {
            while (p < e && *p >= '0' && *p <= '9')
                ++p;
        }

        /// @brief Validate a JSON string literal ("...").
        ///
        /// Handles escape sequences by skipping the escaped character.
        /// Does not validate the escape sequence content — any character
        /// following a backslash is accepted.
        ///
        /// @param p Reference to the current parse position (must point to the
        ///          opening quote); advanced past the closing quote.
        /// @param e Pointer to the end of the input buffer.
        /// @return true if a valid JSON string was consumed.
        consteval bool scan_string(const char *&p, const char *e)
        {
            ++p; // skip opening quote
            while (p < e && *p != '"')
            {
                if (*p == '\\')
                {
                    ++p; // skip backslash
                    if (p >= e)
                        return false;
                }
                ++p; // skip escaped character or regular character
            }
            if (p >= e)
                return false;
            ++p; // skip closing quote
            return true;
        }

        /// @brief Validate a JSON number (integer, floating-point, or scientific).
        ///
        /// Recognises an optional leading minus, an integer part (one or more
        /// digits), an optional fractional part (.digits), and an optional
        /// exponent part (e or E, optional sign, digits).
        ///
        /// @param p Reference to the current parse position; advanced past the number.
        /// @param e Pointer to the end of the input buffer.
        /// @return true if a valid JSON number was consumed.
        consteval bool validate_number(const char *&p, const char *e)
        {
            if (p < e && *p == '-')
                ++p; // optional minus
            if (p >= e || *p < '0' || *p > '9')
                return false;
            skip_digits(p, e); // integer part

            // fractional part: .digits
            if (p < e && *p == '.')
            {
                ++p;
                if (p >= e || *p < '0' || *p > '9')
                    return false;
                skip_digits(p, e);
            }

            // exponent part: e[+/-]digits
            if (p < e && (*p == 'e' || *p == 'E'))
            {
                ++p;
                if (p < e && (*p == '-' || *p == '+'))
                    ++p; // optional sign
                if (p >= e || *p < '0' || *p > '9')
                    return false;
                skip_digits(p, e);
            }
            return true;
        }

        /// @brief Validate any JSON value.
        ///
        /// Dispatches by the first non-whitespace character:
        ///   - 't', 'f', 'n' → match_literal()
        ///   - '"'           → scan_string()
        ///   - '['           → array()
        ///   - '{'           → object()
        ///   - default       → validate_number()
        ///
        /// @param p Reference to the current parse position; advanced past the value.
        /// @param e Pointer to the end of the input buffer.
        /// @return true if a valid JSON value was consumed.
        consteval bool value(const char *&p, const char *e)
        {
            skip_whitespace(p, e);
            if (p >= e)
                return false;

            switch (*p)
            {
            case 't':
                return match_literal(p, e, "true", 4);
            case 'f':
                return match_literal(p, e, "false", 5);
            case 'n':
                return match_literal(p, e, "null", 4);
            case '"':
                return scan_string(p, e);
            case '[':
                return array(p, e);
            case '{':
                return object(p, e);
            default:
                return validate_number(p, e);
            }
        }

        /// @brief Validate a JSON array: [...] with comma-separated values.
        ///
        /// @param p Reference to the current parse position (must point to the
        ///          opening bracket); advanced past the closing bracket.
        /// @param e Pointer to the end of the input buffer.
        /// @return true if a valid JSON array was consumed.
        consteval bool array(const char *&p, const char *e)
        {
            ++p; // skip '['
            skip_whitespace(p, e);
            if (p < e && *p == ']')
            {
                ++p;
                return true; // empty array
            }

            for (;;)
            {
                if (!value(p, e))
                    return false;
                skip_whitespace(p, e);
                if (p >= e)
                    return false;
                if (*p == ']')
                {
                    ++p; // end of array
                    return true;
                }
                if (*p != ',')
                    return false; // missing comma
                ++p;              // skip ','
                skip_whitespace(p, e);
            }
        }

        /// @brief Validate a JSON object: {...} with comma-separated key:value pairs.
        ///
        /// Each key is validated via scan_string().  Each value is validated
        /// via value().
        ///
        /// @param p Reference to the current parse position (must point to the
        ///          opening brace); advanced past the closing brace.
        /// @param e Pointer to the end of the input buffer.
        /// @return true if a valid JSON object was consumed.
        consteval bool object(const char *&p, const char *e)
        {
            ++p; // skip '{'
            skip_whitespace(p, e);
            if (p < e && *p == '}')
            {
                ++p;
                return true; // empty object
            }

            for (;;)
            {
                skip_whitespace(p, e);
                if (p >= e || *p != '"')
                    return false; // key must be a string

                if (!scan_string(p, e))
                    return false; // invalid key

                skip_whitespace(p, e);
                if (p >= e || *p != ':')
                    return false; // missing colon
                ++p;              // skip ':'

                if (!value(p, e))
                    return false; // invalid value

                skip_whitespace(p, e);
                if (p >= e)
                    return false;
                if (*p == '}')
                {
                    ++p; // end of object
                    return true;
                }
                if (*p != ',')
                    return false; // missing comma
                ++p;              // skip ','
            }
        }

    } // namespace validate

} // namespace pjh::json

#endif
