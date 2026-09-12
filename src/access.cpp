#include "pjh_json/access.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace pjh::json
{
    namespace detail
    {
        // Static kind name of the node's active tag (last branch = object:
        // the eight tags are exhaustive, so no default is needed).
        std::string_view type_name(const Json &node) noexcept
        {
            if (node.is_null())
                return "null";
            if (node.is_boolean())
                return "boolean";
            if (node.is_int())
                return "integer";
            if (node.is_float())
                return "number";
            if (node.is_string())
                return "string";
            if (node.is_array())
                return "array";
            return "object";
        }

        // Diagnostic rendering: '.' joins a key, "[n]" an index. Not a
        // round-trippable form (keys may contain the delimiters).
        std::string render_path(const Path &path)
        {
            std::string out;
            for (const auto &step : path)
            {
                if (const auto *key = std::get_if<std::string_view>(&step))
                {
                    if (!out.empty())
                        out.push_back('.');
                    out.append(*key);
                }
                else
                {
                    out.push_back('[');
                    out += std::to_string(std::get<size_t>(step));
                    out.push_back(']');
                }
            }
            return out;
        }
    }

    namespace
    {
        /*
         * Key step read as an array index (parent-decides rule)
         *
         * All-digit, leading zeros accepted; overflow is NOT a grammar error
         * here — the step was parsed as a key and only now attempts an index
         * interpretation, so failure is a hop miss.
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
         * Result mirror of src/path.cpp's walk(): same parent-decides rule,
         * but the first failing hop becomes an AccessError instead of an
         * exception. prefix accumulates the rendered path including the
         * failing hop.
         */
        template <class J>
        pjh::result::Result<J *, AccessError> walk(J &root, const Path &path)
        {
            using R = pjh::result::Result<J *, AccessError>;
            J *cur = &root;
            Path prefix;
            prefix.reserve(path.size());
            for (size_t i = 0; i < path.size(); ++i)
            {
                const PathStep &step = path[i];
                prefix.push_back(step);

                if (const auto *key = std::get_if<std::string_view>(&step))
                {
                    if (cur->is_object())
                    {
                        auto &obj = cur->as_object();
                        if (!obj.contains(*key))
                            return R::Err(AccessError(AccessErrorKind::Missing,
                                                      detail::render_path(prefix),
                                                      "object", {}, i));
                        cur = &obj.at(*key);
                    }
                    else if (cur->is_array())
                    {
                        size_t idx;
                        if (!to_index(*key, idx))
                            return R::Err(AccessError(AccessErrorKind::InvalidIndex,
                                                      detail::render_path(prefix),
                                                      "array", {}, i));
                        auto &arr = cur->as_array();
                        if (idx >= arr.size())
                            return R::Err(AccessError(AccessErrorKind::OutOfRange,
                                                      detail::render_path(prefix),
                                                      "array", {}, i));
                        cur = &arr.at(idx);
                    }
                    else
                        return R::Err(AccessError(AccessErrorKind::TypeMismatch,
                                                  detail::render_path(prefix),
                                                  "object", detail::type_name(*cur), i));
                }
                else
                {
                    const size_t idx = std::get<size_t>(step);
                    if (cur->is_array())
                    {
                        auto &arr = cur->as_array();
                        if (idx >= arr.size())
                            return R::Err(AccessError(AccessErrorKind::OutOfRange,
                                                      detail::render_path(prefix),
                                                      "array", {}, i));
                        cur = &arr.at(idx);
                    }
                    else if (cur->is_object())
                    {
                        const std::string key = std::to_string(idx);
                        auto &obj = cur->as_object();
                        if (!obj.contains(key))
                            return R::Err(AccessError(AccessErrorKind::Missing,
                                                      detail::render_path(prefix),
                                                      "object", {}, i));
                        cur = &obj.at(key);
                    }
                    else
                        return R::Err(AccessError(AccessErrorKind::TypeMismatch,
                                                  detail::render_path(prefix),
                                                  "array", detail::type_name(*cur), i));
                }
            }
            return R::Ok(cur);
        }
    }

    // --- find_path_result ---

    pjh::result::Result<Json *, AccessError>
    find_path_result(Json &root, const Path &path)
    {
        return walk(root, path);
    }

    pjh::result::Result<const Json *, AccessError>
    find_path_result(const Json &root, const Path &path)
    {
        return walk(root, path);
    }

    pjh::result::Result<Json *, AccessError>
    find_path_result(Json &root, std::string_view path)
    {
        try
        {
            return walk(root, parse_path(path));
        }
        catch (const std::invalid_argument &)
        {
            return pjh::result::Result<Json *, AccessError>::Err(
                AccessError(AccessErrorKind::MalformedPath, std::string(path)));
        }
    }

    pjh::result::Result<const Json *, AccessError>
    find_path_result(const Json &root, std::string_view path)
    {
        try
        {
            return walk(root, parse_path(path));
        }
        catch (const std::invalid_argument &)
        {
            return pjh::result::Result<const Json *, AccessError>::Err(
                AccessError(AccessErrorKind::MalformedPath, std::string(path)));
        }
    }
}
