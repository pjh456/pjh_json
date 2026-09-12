#ifndef INCLUDE_PJH_JSON_ACCESS_HPP
#define INCLUDE_PJH_JSON_ACCESS_HPP

#include <concepts>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include <pjh_result/result.hpp>
#include "error.hpp"
#include "json.hpp"
#include "path.hpp"

namespace pjh::json
{
    namespace detail
    {
        /**
         * @brief Static JSON kind name of a node ("null"/"boolean"/...)
         */
        [[nodiscard]] std::string_view type_name(const Json &node) noexcept;

        /**
         * @brief Canonical diagnostic rendering of a typed path
         */
        [[nodiscard]] std::string render_path(const Path &path);

        template <class T>
        [[nodiscard]] constexpr std::string_view expected_name() noexcept
        {
            if constexpr (std::same_as<T, bool>)
                return "boolean";
            else if constexpr (std::same_as<T, int64_t>)
                return "integer";
            else if constexpr (std::same_as<T, std::string_view>)
                return "string";
            else
                return "number";
        }

        [[nodiscard]] inline AccessError type_mismatch(
            const Json &node, std::string display, std::string_view expected)
        {
            return AccessError(AccessErrorKind::TypeMismatch, std::move(display),
                               expected, type_name(node));
        }

        template <class T>
            requires(std::same_as<T, bool> || std::same_as<T, int64_t> ||
                     std::same_as<T, float> || std::same_as<T, double> ||
                     std::same_as<T, std::string_view>)
        [[nodiscard]] pjh::result::Result<T, AccessError>
        extract(const Json &node, std::string display)
        {
            using R = pjh::result::Result<T, AccessError>;
            if constexpr (std::same_as<T, bool>)
            {
                auto v = node.try_as_boolean();
                if (!v)
                    return R::Err(type_mismatch(node, std::move(display), expected_name<T>()));
                return R::Ok(*v);
            }
            else if constexpr (std::same_as<T, int64_t>)
            {
                auto v = node.try_get<int64_t>();
                if (!v)
                    return R::Err(type_mismatch(node, std::move(display), expected_name<T>()));
                return R::Ok(*v);
            }
            else if constexpr (std::same_as<T, float> || std::same_as<T, double>)
            {
                auto v = node.try_get<T>();
                if (!v)
                    return R::Err(type_mismatch(node, std::move(display), expected_name<T>()));
                return R::Ok(*v);
            }
            else
            {
                auto v = node.try_as_string();
                if (!v)
                    return R::Err(type_mismatch(node, std::move(display), expected_name<T>()));
                return R::Ok(*v);
            }
        }
    }

    /**
     * @brief Result twin of find_path: typed miss instead of nullptr
     * @param root Root node to walk
     * @param path Dotted/bracket path; empty = root
     * @return Result holding the resolved node, or the AccessError describing
     *         the first failing hop (MalformedPath when the DSL fails to parse)
     * @note The parent node's runtime type decides each hop, exactly as
     *       at_path/find_path (an index step over an object parent resolves
     *       its decimal string as the key name; a key step over an array
     *       parent must be all-digit).
     */
    [[nodiscard]] pjh::result::Result<Json *, AccessError>
    find_path_result(Json &root, std::string_view path);

    /**
     * @brief Const result twin of find_path
     * @param root Root node to walk
     * @param path Dotted/bracket path; empty = root
     * @return Result holding the resolved node, or the AccessError describing
     *         the first failing hop
     */
    [[nodiscard]] pjh::result::Result<const Json *, AccessError>
    find_path_result(const Json &root, std::string_view path);

    /**
     * @brief Result twin of find_path by typed path (no parsing)
     * @param root Root node to walk
     * @param path Sequence of key/index hops; empty = root
     * @return Result holding the resolved node, or the AccessError describing
     *         the first failing hop
     * @note The typed form is the escape hatch for keys containing '.', '['
     *       or ']' (MalformedPath is not reachable here)
     */
    [[nodiscard]] pjh::result::Result<Json *, AccessError>
    find_path_result(Json &root, const Path &path);

    /**
     * @brief Const result twin of find_path by typed path
     * @param root Root node to walk
     * @param path Sequence of key/index hops; empty = root
     * @return Result holding the resolved node, or the AccessError describing
     *         the first failing hop
     */
    [[nodiscard]] pjh::result::Result<const Json *, AccessError>
    find_path_result(const Json &root, const Path &path);

    /**
     * @brief Typed extraction along a path (Result form)
     *
     * Resolves the path with find_path_result, then converts the node to T:
     * bool via try_as_boolean, int64_t via try_get<int64_t> (Integer only),
     * float/double via try_get<T> (Integer and Floating, widening allowed),
     * std::string_view via try_as_string. A resolution failure is forwarded
     * unchanged; a conversion failure yields TypeMismatch with the full
     * request path, the expected kind name and the node's actual kind name.
     *
     * @tparam T bool, int64_t, float, double or std::string_view
     * @param root Root node to walk
     * @param path Dotted/bracket path; empty = root
     * @return Result holding the converted value, or an AccessError
     * @throws std::bad_alloc on allocation failure while building the error
     * @note get_path<std::string_view> borrows the Document buffer; keep the
     *       root's storage alive while the view is used (same contract as
     *       try_as_string).
     */
    template <class T>
        requires(std::same_as<T, bool> || std::same_as<T, int64_t> ||
                 std::same_as<T, float> || std::same_as<T, double> ||
                 std::same_as<T, std::string_view>)
    [[nodiscard]] pjh::result::Result<T, AccessError>
    get_path(const Json &root, std::string_view path)
    {
        using R = pjh::result::Result<T, AccessError>;
        auto found = find_path_result(root, path);
        if (found.is_err())
            return R::Err(std::move(found).unwrap_err());
        return detail::extract<T>(*found.unwrap(), std::string(path));
    }

    /**
     * @brief Typed extraction along a typed path (Result form)
     * @tparam T bool, int64_t, float, double or std::string_view
     * @param root Root node to walk
     * @param path Sequence of key/index hops; empty = root
     * @return Result holding the converted value, or an AccessError
     * @throws std::bad_alloc on allocation failure while building the error
     * @note Same conversion contract as get_path(root, string_view).
     */
    template <class T>
        requires(std::same_as<T, bool> || std::same_as<T, int64_t> ||
                 std::same_as<T, float> || std::same_as<T, double> ||
                 std::same_as<T, std::string_view>)
    [[nodiscard]] pjh::result::Result<T, AccessError>
    get_path(const Json &root, const Path &path)
    {
        using R = pjh::result::Result<T, AccessError>;
        auto found = find_path_result(root, path);
        if (found.is_err())
            return R::Err(std::move(found).unwrap_err());
        return detail::extract<T>(*found.unwrap(), detail::render_path(path));
    }
}

#endif // INCLUDE_PJH_JSON_ACCESS_HPP
