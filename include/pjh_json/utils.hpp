#ifndef INCLUDE_PJH_JSON_UTILS_HPP
#define INCLUDE_PJH_JSON_UTILS_HPP

#include "error.hpp"

#include <cstddef>
#include <string>

namespace pjh::json
{

#ifdef NDEBUG
#define PJH_JSON_NOEXCEPT noexcept

    template <typename T>
    constexpr void debug_check_type(T, T, const char *) noexcept
    {
    }

    template <typename T>
    constexpr void debug_check_type2(T, T, T, const char *) noexcept
    {
    }

#else
#define PJH_JSON_NOEXCEPT

    template <typename T>
    inline void debug_check_type(T actual, T expected, const char *name)
    {
        if (actual != expected)
            throw TypeError(
                std::string("type mismatch in as_") + name + "()");
    }

    template <typename T>
    inline void debug_check_type2(T actual, T a, T b, const char *name)
    {
        if (actual != a && actual != b)
            throw TypeError(
                std::string("type mismatch in as_") + name + "()");
    }

#endif

#ifdef NDEBUG
    [[noreturn]] inline void throw_parse_error(const char *, const char *, const char *)
    {
        throw ParseError("parse error");
    }
#else
    [[noreturn]] inline void throw_parse_error(const char *msg, const char *curr, const char *begin)
    {
        auto off = static_cast<size_t>(curr - begin);
        throw ParseError(std::string(msg) + " at offset " + std::to_string(off));
    }
#endif

    uint32_t parse_hex4(const char *&curr, const char *begin);
    void encode_utf8(uint32_t cp, char *&dst);
    void handle_escape(char *&dst, const char *&m_curr, const char *m_begin);

}

#endif
