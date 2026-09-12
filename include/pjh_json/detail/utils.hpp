#ifndef INCLUDE_PJH_JSON_DETAIL_UTILS_HPP
#define INCLUDE_PJH_JSON_DETAIL_UTILS_HPP

#include "pjh_json/error.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace pjh::json
{

    [[noreturn]] inline void throw_parse_error(const char *msg, const char *curr, const char *begin)
    {
        auto off = static_cast<size_t>(curr - begin);
        throw ParseError(std::string(msg) + " at offset " + std::to_string(off), off);
    }

    uint32_t parse_hex4(const char *&curr, const char *begin);
    void encode_utf8(uint32_t cp, char *&dst);
    void handle_escape(char *&dst, const char *&m_curr, const char *m_begin);

} // namespace pjh::json

#endif // INCLUDE_PJH_JSON_DETAIL_UTILS_HPP
