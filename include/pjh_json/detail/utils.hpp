#ifndef INCLUDE_PJH_JSON_DETAIL_UTILS_HPP
#define INCLUDE_PJH_JSON_DETAIL_UTILS_HPP

#include "pjh_json/error.hpp"
#include "pjh_json/grammar.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace pjh::json
{

    class Json;

    [[noreturn]] inline void throw_parse_error(const char *msg, const char *curr, const char *begin)
    {
        auto off = static_cast<size_t>(curr - begin);
        throw ParseError(std::string(msg) + " at offset " + std::to_string(off), off);
    }

    // On success returns ErrorCode::None; on failure returns the key and sets
    // err_pos to the offending input byte. Offsets are computed by the caller
    // (Parser::fail) from err_pos.
    ErrorCode parse_hex4(const char *&curr, uint32_t &out, const char *&err_pos);
    // returns false => InvalidCodepoint (context-free)
    bool encode_utf8(uint32_t cp, char *&dst);
    // On success returns ErrorCode::None; err_pos==nullptr => context-free
    // (InvalidCodepoint).
    ErrorCode handle_escape(char *&dst, const char *&curr, const char *&err_pos);

    // Classify a grammar-scanned RFC 8259 number token into out: int64 when it
    // fits, otherwise a finite double. On success returns ErrorCode::None and
    // leaves out set; on failure returns the key and sets err_pos to
    // NumberOutOfRange at the token start or NumberInvalidFormat at the token
    // end. The token must already have passed grammar::scan_number (scan is
    // that result, whose pointers alias [token_begin, token_end)).
    //
    // Shared by Parser::parse_number and the streaming event core so the two
    // cannot drift on number value classification (task 35.2).
    ErrorCode classify_number(const char *token_begin, const char *token_end, const grammar::number_scan &scan,
                              Json &out, const char *&err_pos) noexcept;

} // namespace pjh::json

#endif // INCLUDE_PJH_JSON_DETAIL_UTILS_HPP
