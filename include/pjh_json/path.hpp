#ifndef INCLUDE_PJH_JSON_PATH_HPP
#define INCLUDE_PJH_JSON_PATH_HPP

#include <string_view>

#include "json.hpp"

namespace pjh::json
{
    /**
     * @brief Compile a dotted/bracket path string into typed hops
     *
     * Grammar: path = '' | segment ('.' segment)*; segment = key bracket*;
     * bracket = '[' digits ']' (digits = [0-9]+, value <= SIZE_MAX).
     * The empty path is the root (zero hops). Keys pass through as raw
     * bytes between the ASCII delimiters (UTF-8 safe, no interpretation,
     * no escape sequences); an interior ".." is the empty key.
     *
     * @param path e.g. "a.b[3].c" -> {key a, key b, index 3, key c}; empty = root
     * @return Typed path, ready for at_path/find_path/contains
     * @throws std::invalid_argument on malformed grammar (unbalanced or
     *         empty bracket, non-digit/negative/overflowing index, leading
     *         or trailing '.', junk after a bracket)
     * @note A key containing '.', '[' or ']' cannot be expressed in this
     *       DSL — build the Path directly instead.
     * @note Leading/trailing '.' is malformed (a documented tightening vs
     *       RFC 6901's boundary-empty tokens).
     * @warning The returned Path's string_view key steps reference path's
     *       buffer: the result is valid only while path's buffer stays
     *       alive. parse_path does not copy; for a durable path keep the
     *       source string alive or build the Path from longer-lived keys.
     */
    [[nodiscard]] Path parse_path(std::string_view path);
}

#endif // INCLUDE_PJH_JSON_PATH_HPP
