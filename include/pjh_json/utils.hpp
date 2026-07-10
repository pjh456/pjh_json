#ifndef INCLUDE_PJH_JSON_UTILS_HPP
#define INCLUDE_PJH_JSON_UTILS_HPP

#include "error.hpp"

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

}

#endif
