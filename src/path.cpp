#include "pjh_json/path.hpp"
#include "pjh_json/json.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace pjh::json
{
    // --- parse_path ---

    /*
     * Single-pass dotted/bracket grammar compiler (no backtracking)
     *
     * 1. Pre-check: a leading or trailing '.' is malformed (documented
     *    tightening vs RFC 6901's boundary-empty tokens); interior ".."
     *    is the empty key.
     * 2. Per segment: the key part is raw bytes up to '.' or '['; it emits
     *    a key step when non-empty or when reached through a '.' separator
     *    (so "a..b" keeps its empty key, while "[5]" is a bare index hop).
     * 3. Brackets: digits only, value <= SIZE_MAX (overflow is a grammar
     *    error); zero digits, a non-digit, or a missing ']' is malformed.
     * 4. After the brackets of a segment only a '.' separator may follow.
     */
    Path parse_path(std::string_view path)
    {
        if (path.empty())
            return Path{};
        if (path.front() == '.' || path.back() == '.')
            throw std::invalid_argument("path: boundary '.'");

        Path steps;
        const char *p = path.data();
        const char *const end = p + path.size();
        bool via_dot = false;
        while (p < end)
        {
            const char *seg = p;
            while (p < end && *p != '.' && *p != '[')
                ++p;
            if (p > seg || via_dot)
                steps.emplace_back(std::string_view(seg, size_t(p - seg)));
            for (;;)
            {
                if (p >= end || *p != '[')
                    break;
                ++p; // past '['
                const char *digits = p;
                size_t idx = 0;
                while (p < end && *p >= '0' && *p <= '9')
                {
                    if (idx > (SIZE_MAX - 9) / 10)
                        throw std::invalid_argument("path: index overflow");
                    idx = idx * 10 + size_t(*p - '0');
                    ++p;
                }
                if (p == digits)
                    throw std::invalid_argument("path: empty index");
                if (p >= end || *p != ']')
                    throw std::invalid_argument("path: invalid index");
                ++p; // past ']'
                steps.emplace_back(idx);
            }
            if (p < end)
            {
                if (*p != '.')
                    throw std::invalid_argument("path: junk after bracket");
                ++p; // past '.'
                via_dot = true;
            }
            else
                via_dot = false;
        }
        return steps;
    }

    // --- walker ---

    namespace
    {
        /*
         * Key step read as an array index (parent-decides rule)
         *
         * All-digit, leading zeros accepted; overflow is NOT a grammar
         * error here — the step was parsed as a key and only now attempts
         * an index interpretation, so failure is a hop miss.
         */
        bool to_index(std::string_view s, size_t &out)
        {
            if (s.empty())
                return false;
            size_t v = 0;
            for (char c : s)
            {
                if (c < '0' || c > '9')
                    return false;
                if (v > (SIZE_MAX - 9) / 10)
                    return false; // overflow -> not an index
                v = v * 10 + size_t(c - '0');
            }
            out = v;
            return true;
        }

        /*
         * One hop; exceptions are the composed-at() contract (per-hop)
         *
         * 1. Key step: object parent -> at(key) (missing key ->
         *    out_of_range); array parent -> all-digit key as index, else
         *    out_of_range; scalar parent -> TypeError "expected object"
         *    (mirrors a direct Json::at(key)).
         * 2. Index step: array parent -> at(idx) (OOB -> out_of_range);
         *    object parent -> decimal string as key name; scalar parent ->
         *    TypeError "expected array" (mirrors a direct Json::at(idx)).
         */
        template <class J>
        J &hop(J &node, const PathStep &step)
        {
            if (const auto *key = std::get_if<std::string_view>(&step))
            {
                if (node.is_object())
                    return node.at(*key);
                if (node.is_array())
                {
                    size_t idx;
                    if (!to_index(*key, idx))
                        throw std::out_of_range("not a valid array index");
                    return node.at(idx);
                }
                throw TypeError("expected object");
            }
            const size_t idx = std::get<std::size_t>(step);
            if (node.is_array())
                return node.at(idx);
            if (node.is_object())
                return node.at(std::to_string(idx));
            throw TypeError("expected array");
        }

        /*
         * Walk the full path from root; empty path yields root
         *
         * Pointer-chasing (not reference reassignment): Json is move-only,
         * so a Json& cannot be "moved" between hops.
         */
        template <class J>
        J &walk(J &root, const Path &path)
        {
            J *cur = &root;
            for (const auto &s : path)
                cur = &hop(*cur, s);
            return *cur;
        }
    }

    // --- at_path ---

    Json &Json::at_path(const Path &path) { return walk(*this, path); }
    const Json &Json::at_path(const Path &path) const { return walk(*this, path); }
    Json &Json::at_path(std::string_view path)
    {
        return walk(*this, parse_path(path));
    }
    const Json &Json::at_path(std::string_view path) const
    {
        return walk(*this, parse_path(path));
    }

    // --- find_path ---

    Json *Json::find_path(const Path &path)
    {
        try
        {
            return &walk(*this, path);
        }
        catch (const TypeError &)
        {
            return nullptr;
        }
        catch (const std::out_of_range &)
        {
            return nullptr;
        }
    }

    const Json *Json::find_path(const Path &path) const
    {
        try
        {
            return &walk(*this, path);
        }
        catch (const TypeError &)
        {
            return nullptr;
        }
        catch (const std::out_of_range &)
        {
            return nullptr;
        }
    }

    Json *Json::find_path(std::string_view path)
    {
        const Path steps = parse_path(path); // outside try: invalid_argument penetrates
        try
        {
            return &walk(*this, steps);
        }
        catch (const TypeError &)
        {
            return nullptr;
        }
        catch (const std::out_of_range &)
        {
            return nullptr;
        }
    }

    const Json *Json::find_path(std::string_view path) const
    {
        const Path steps = parse_path(path); // outside try: invalid_argument penetrates
        try
        {
            return &walk(*this, steps);
        }
        catch (const TypeError &)
        {
            return nullptr;
        }
        catch (const std::out_of_range &)
        {
            return nullptr;
        }
    }

    // --- contains ---

    bool Json::contains(const Path &path) const noexcept
    {
        try
        {
            walk(*this, path);
            return true;
        }
        catch (const TypeError &)
        {
            return false;
        }
        catch (const std::out_of_range &)
        {
            return false;
        }
    }

    bool Json::contains(std::string_view path) const
    {
        return contains(parse_path(path));
    }
}
