#ifndef INCLUDE_PJH_JSON_SCHEMA_HPP
#define INCLUDE_PJH_JSON_SCHEMA_HPP

#include "access.hpp"
#include "error.hpp"
#include "json.hpp"
#include "json_constexpr.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <pjh_result/result.hpp>

/// @file schema.hpp
/// @brief Lightweight JSON-Schema-inspired validation of a runtime Json tree
///        against a compile-time ConstJson schema (opt-in module).
///
/// The schema is an ordinary `ConstJson::of(kv(...))` value, so it carries no
/// runtime parsing cost and no heap allocation. `validate_result()` reads the
/// schema's public tuple members at runtime and recurses by schema type; the
/// schema type is part of the call signature.
///
/// @note This header is deliberately NOT part of the umbrella
///       `include/pjh_json.hpp`: including it pulls in `json_constexpr.hpp`
///       (and thus `validate.hpp`), which the umbrella keeps opt-in (task 59).
/// @note This is a JSON-Schema-*inspired* keyword subset, not a conformant
///       draft 2020-12 implementation. Only `type`, `required`, `properties`
///       and `items` are implemented in the structural core (38.1). Unknown
///       keywords are ignored for forward compatibility.
/// @note Documented deviations from JSON Schema:
///       - `"type":"integer"` is tag-based (`Json::is_int()`): `1.0` parses as
///         a Floating node here and is rejected, although the mathematical
///         JSON Schema definition would call it an integer. `"number"` accepts
///         both Integer and Floating.
///       - Keywords apply only inside their domain (a `required` keyword is
///         ignored for a non-object instance) and the first failure
///         short-circuits.
///       - `"type"` is always evaluated first regardless of authored order;
///         the remaining keywords run in authored order.
///       - The schema must be a `ConstJsonObject`; a scalar or array schema is
///         reported as `SchemaErrorKind::InvalidSchema`.
///       - A malformed keyword parameter (e.g. a non-string `type`, a
///         non-array `required`) is reported at runtime as
///         `SchemaErrorKind::InvalidSchema`, not as a compile error.
/// @note `ConstJson::parse()` only returns a validity verdict and cannot build
///       a schema value tree, so a schema can only be authored in C++ source.

namespace pjh::json::schema
{
    namespace detail
    {
        using schema_result = pjh::result::Result<void, SchemaError>;

        /// @brief Shared success value (Result<void, SchemaError>).
        [[nodiscard]] inline schema_result ok() noexcept
        {
            return schema_result::Ok();
        }

        /// @brief Build an `InvalidSchema` failure for a malformed keyword.
        [[nodiscard]] inline schema_result invalid_schema(std::string path, std::string keyword,
                                                          std::string expected = {}, std::string actual = {})
        {
            return schema_result::Err(SchemaError(SchemaErrorKind::InvalidSchema, std::move(path), std::move(expected),
                                                  std::move(actual), std::move(keyword)));
        }

        /// @brief Diagnostic path accumulator ("user.tags[2]"), rewound on return.
        struct PathBuf
        {
            std::string s;

            /// @brief Append a ".key" hop; returns the mark to rewind to.
            std::size_t push_key(std::string_view k)
            {
                const std::size_t mark = s.size();
                if (!s.empty())
                    s.push_back('.');
                s.append(k);
                return mark;
            }

            /// @brief Append an "[i]" hop; returns the mark to rewind to.
            std::size_t push_index(std::size_t i)
            {
                const std::size_t mark = s.size();
                s.push_back('[');
                s += std::to_string(i);
                s.push_back(']');
                return mark;
            }

            /// @brief Drop hops appended after @p mark.
            void rewind(std::size_t mark) { s.resize(mark); }
        };

        // ---- local ConstJson traits (no dependency on task 37.1 helpers) ----

        /// @brief Trait: true when T (decayed) is a ConstJsonObject.
        template <typename T> struct is_const_json_object : std::false_type
        {
        };

        /// @brief Specialization for ConstJsonObject.
        template <typename... Es> struct is_const_json_object<ConstJsonObject<Es...>> : std::true_type
        {
        };

        /// @brief Convenience variable template for is_const_json_object.
        template <typename T>
        inline constexpr bool is_const_json_object_v = is_const_json_object<std::decay_t<T>>::value;

        /// @brief Trait: true when T (decayed) is a ConstJsonArray.
        template <typename T> struct is_const_json_array : std::false_type
        {
        };

        /// @brief Specialization for ConstJsonArray.
        template <typename... Ts> struct is_const_json_array<ConstJsonArray<Ts...>> : std::true_type
        {
        };

        /// @brief Convenience variable template for is_const_json_array.
        template <typename T> inline constexpr bool is_const_json_array_v = is_const_json_array<std::decay_t<T>>::value;

        /// @brief Trait: true when E is a ConstJsonStr usable as a keyword string.
        template <typename E> inline constexpr bool is_const_json_str_v = std::is_same_v<std::decay_t<E>, ConstJsonStr>;

        // ---- forward declarations (mutually recursive validation) ---------

        template <typename V>
        [[nodiscard]] schema_result validate_node(const V &schema, const Json &value, PathBuf &pb);

        template <typename V>
        [[nodiscard]] schema_result apply_keyword(std::string_view keyword, const V &schema_value, const Json &value,
                                                  PathBuf &pb);

        /// @brief Short-circuiting left fold over a ConstJsonObject's entries.
        ///
        /// Uses an `index_sequence` expansion rather than `std::apply`, which
        /// is what keeps the heterogeneous tuple dispatch portable to MSVC.
        template <typename... Es, typename F>
        [[nodiscard]] schema_result for_each_entry(const ConstJsonObject<Es...> &o, F &&f)
        {
            schema_result res = ok();
            [&]<std::size_t... I>(std::index_sequence<I...>)
            {
                ((res.is_ok() ? (void)(res = f(std::get<I>(o.entries))) : (void)0), ...);
            }(std::index_sequence_for<Es...>{});
            return res;
        }

        /// @brief Map a declared `type` name to the instance's tag and compare.
        ///
        /// An unknown name is `InvalidSchema`; a known-but-different kind is
        /// `TypeMismatch`.
        [[nodiscard]] inline schema_result check_type(std::string_view expected_type, const Json &value, PathBuf &pb)
        {
            bool known = true;
            bool matches = false;
            if (expected_type == "null")
                matches = value.is_null();
            else if (expected_type == "boolean")
                matches = value.is_boolean();
            else if (expected_type == "integer")
                matches = value.is_int();
            else if (expected_type == "number")
                matches = value.is_number();
            else if (expected_type == "string")
                matches = value.is_string();
            else if (expected_type == "array")
                matches = value.is_array();
            else if (expected_type == "object")
                matches = value.is_object();
            else
                known = false;

            if (!known)
                return invalid_schema(pb.s, "type", "known JSON type name", std::string(expected_type));
            if (matches)
                return ok();
            return schema_result::Err(SchemaError(SchemaErrorKind::TypeMismatch, pb.s, std::string(expected_type),
                                                  std::string(pjh::json::detail::type_name(value)), "type"));
        }

        // ---- required ------------------------------------------------------

        /// @brief `required` shape pass: every element must be a ConstJsonStr.
        template <typename E> [[nodiscard]] schema_result required_element_shape(const E &, PathBuf &pb)
        {
            if constexpr (is_const_json_str_v<E>)
                return ok();
            else
                return invalid_schema(pb.s, "required", "array of strings");
        }

        /// @brief `required` check for one key against an object instance.
        template <typename E> [[nodiscard]] schema_result required_one(const E &element, const Object &obj, PathBuf &pb)
        {
            if constexpr (is_const_json_str_v<E>)
            {
                if (obj.contains(element.v))
                    return ok();
                const std::size_t mark = pb.push_key(element.v);
                schema_result r = schema_result::Err(
                    SchemaError(SchemaErrorKind::MissingRequired, pb.s, "present", "missing", "required"));
                pb.rewind(mark);
                return r;
            }
            else
            {
                (void)element;
                (void)obj;
                return ok(); // already reported by the shape pass
            }
        }

        /// @brief `required`: a ConstJsonStr array applied to an object instance.
        template <typename V>
        [[nodiscard]] schema_result check_required(const V &schema_value, const Json &value, PathBuf &pb)
        {
            if constexpr (!is_const_json_array_v<V>)
            {
                return invalid_schema(pb.s, "required", "array of strings");
            }
            else
            {
                constexpr std::size_t n = std::tuple_size_v<std::decay_t<decltype(schema_value.elems)>>;

                schema_result shape = ok();
                [&]<std::size_t... I>(std::index_sequence<I...>)
                {
                    ((shape.is_ok() ? (void)(shape = required_element_shape(std::get<I>(schema_value.elems), pb))
                                    : (void)0),
                     ...);
                }(std::make_index_sequence<n>{});
                if (shape.is_err())
                    return shape;

                if (!value.is_object())
                    return ok(); // keyword outside its domain

                const Object &obj = value.as_object();
                schema_result res = ok();
                [&]<std::size_t... I>(std::index_sequence<I...>)
                {
                    ((res.is_ok() ? (void)(res = required_one(std::get<I>(schema_value.elems), obj, pb)) : (void)0),
                     ...);
                }(std::make_index_sequence<n>{});
                return res;
            }
        }

        // ---- properties ----------------------------------------------------

        /// @brief `properties` check for one declared key that exists.
        template <typename E> [[nodiscard]] schema_result property_one(const E &entry, const Object &obj, PathBuf &pb)
        {
            if (!obj.contains(entry.key))
                return ok();
            const std::size_t mark = pb.push_key(entry.key);
            schema_result r = validate_node(entry.value, obj[entry.key], pb);
            pb.rewind(mark);
            return r;
        }

        /// @brief `properties`: recurse into each declared key that exists.
        template <typename V>
        [[nodiscard]] schema_result check_properties(const V &schema_value, const Json &value, PathBuf &pb)
        {
            if constexpr (!is_const_json_object_v<V>)
            {
                return invalid_schema(pb.s, "properties", "object schema");
            }
            else
            {
                if (!value.is_object())
                    return ok(); // keyword outside its domain

                const Object &obj = value.as_object();
                constexpr std::size_t n = std::tuple_size_v<std::decay_t<decltype(schema_value.entries)>>;

                schema_result res = ok();
                [&]<std::size_t... I>(std::index_sequence<I...>)
                {
                    ((res.is_ok() ? (void)(res = property_one(std::get<I>(schema_value.entries), obj, pb)) : (void)0),
                     ...);
                }(std::make_index_sequence<n>{});
                return res;
            }
        }

        // ---- items ---------------------------------------------------------

        /// @brief `items`: recurse into every element of an array instance.
        template <typename V>
        [[nodiscard]] schema_result check_items(const V &item_schema, const Json &value, PathBuf &pb)
        {
            if constexpr (!is_const_json_object_v<V>)
            {
                return invalid_schema(pb.s, "items", "object schema");
            }
            else
            {
                if (!value.is_array())
                    return ok(); // keyword outside its domain

                const Array &arr = value.as_array();
                for (std::size_t i = 0; i < arr.size(); ++i)
                {
                    const std::size_t mark = pb.push_index(i);
                    schema_result r = validate_node(item_schema, arr[i], pb);
                    pb.rewind(mark);
                    if (r.is_err())
                        return r;
                }
                return ok();
            }
        }

        // ---- keyword dispatch ----------------------------------------------

        /// @brief Dispatch one non-`type` keyword; unknown keywords are ignored.
        template <typename V>
        [[nodiscard]] schema_result apply_keyword(std::string_view keyword, const V &schema_value, const Json &value,
                                                  PathBuf &pb)
        {
            if (keyword == "required")
                return check_required(schema_value, value, pb);
            if (keyword == "properties")
                return check_properties(schema_value, value, pb);
            if (keyword == "items")
                return check_items(schema_value, value, pb);
            return ok();
        }

        // ---- node entry ----------------------------------------------------

        /// @brief Validate @p value against one schema node.
        ///
        /// A schema node must be a ConstJsonObject. `type` runs first, then the
        /// remaining keywords in authored order; the first failure
        /// short-circuits.
        template <typename V> [[nodiscard]] schema_result validate_node(const V &schema, const Json &value, PathBuf &pb)
        {
            if constexpr (!is_const_json_object_v<V>)
            {
                (void)value;
                return invalid_schema(pb.s, "schema", "object schema");
            }
            else
            {
                schema_result type_pass = for_each_entry(schema,
                                                         [&](const auto &entry) -> schema_result
                                                         {
                                                             if (entry.key != "type")
                                                                 return ok();
                                                             using EV = std::decay_t<decltype(entry.value)>;
                                                             if constexpr (std::is_same_v<EV, ConstJsonStr>)
                                                                 return check_type(entry.value.v, value, pb);
                                                             else
                                                                 return invalid_schema(pb.s, "type", "string");
                                                         });
                if (type_pass.is_err())
                    return type_pass;

                return for_each_entry(schema,
                                      [&](const auto &entry) -> schema_result
                                      {
                                          if (entry.key == "type")
                                              return ok();
                                          return apply_keyword(entry.key, entry.value, value, pb);
                                      });
            }
        }
    } // namespace detail

    /**
     * @brief Validate a runtime Json value against a compile-time ConstJson schema
     * @tparam Schema Deduced schema type (a ConstJsonObject)
     * @param schema The schema authored via `ConstJson::of(kv(...))`
     * @param value Runtime value to validate
     * @return Result holding success, or the first SchemaError (see
     *         SchemaErrorKind). Never throws for a schema violation; an
     *         allocation failure while building the error propagates as
     *         std::bad_alloc.
     * @note Evaluation: `type` first, then the remaining keywords in authored
     *       order, first failure short-circuiting. Keywords outside their
     *       instance domain are ignored. See the file header for the
     *       documented deviations from JSON Schema.
     */
    template <typename Schema>
    [[nodiscard]] pjh::result::Result<void, SchemaError> validate_result(const Schema &schema, const Json &value)
    {
        detail::PathBuf pb;
        return detail::validate_node(schema, value, pb);
    }

    /**
     * @brief Throwing shell over validate_result()
     * @tparam Schema Deduced schema type (a ConstJsonObject)
     * @param schema The schema authored via `ConstJson::of(kv(...))`
     * @param value Runtime value to validate
     * @throws SchemaError on the first violation
     */
    template <typename Schema> void validate(const Schema &schema, const Json &value)
    {
        auto result = validate_result(schema, value);
        if (result.is_err())
            throw std::move(result).unwrap_err();
    }
} // namespace pjh::json::schema

#endif
