#ifndef INCLUDE_PJH_JSON_SCHEMA_HPP
#define INCLUDE_PJH_JSON_SCHEMA_HPP

#include "access.hpp"
#include "error.hpp"
#include "json.hpp"
#include "json_constexpr.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
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
///       draft 2020-12 implementation. The structural core (38.1) implements
///       `type`, `required`, `properties` and `items`; the value constraints
///       (38.2) add `enum`, `const`, `minLength`/`maxLength`,
///       `minItems`/`maxItems`, `minimum`/`maximum` and
///       `additionalProperties: false`. Unknown keywords are ignored for
///       forward compatibility.
/// @note Documented deviations from JSON Schema:
///       - `"type":"integer"` is tag-based (`Json::is_int()`): `1.0` parses as
///         a Floating node here and is rejected, although the mathematical
///         JSON Schema definition would call it an integer. `"number"` accepts
///         both Integer and Floating.
///       - Keywords apply only inside their domain (a `required` keyword is
///         ignored for a non-object instance) and the first failure
///         short-circuits.
///       - `enum`/`const` compare with `Json::operator==`, so the comparison
///         is tag-strict: an Integer `1` does not equal a Floating `1.0`.
///       - `minLength`/`maxLength` count Unicode code points (bytes that are
///         not UTF-8 continuation bytes), not raw bytes.
///       - `minimum`/`maximum` are inclusive and accept an Integer or a
///         Floating bound; both Integer and Floating instances are numbers.
///       - `additionalProperties` accepts only a boolean: `false` rejects
///         keys not listed in the same node's `properties`, `true` allows
///         them. The sub-schema form (validate extra keys against a schema)
///         is out of scope for this MVP.
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

        // ---- enum / const --------------------------------------------------

        /// @brief Render a runtime value for an error message (cold path).
        [[nodiscard]] inline std::string value_repr(const Json &value)
        {
            if (value.is_null())
                return "null";
            if (value.is_boolean())
                return value.as_boolean() ? "true" : "false";
            if (value.is_int())
                return std::to_string(value.as_int());
            if (value.is_float())
            {
                char buf[32];
                const auto r = std::to_chars(buf, buf + sizeof(buf), value.as_float());
                return std::string(buf, r.ptr);
            }
            if (value.is_string())
                return "\"" + std::string(value.as_string()) + "\"";
            return std::string(pjh::json::detail::type_name(value));
        }

        /// @brief `enum`: the instance must equal one of the schema array
        ///        members under the `Json::operator==` model.
        template <typename V>
        [[nodiscard]] schema_result check_enum(const V &schema_value, const Json &value, PathBuf &pb)
        {
            if constexpr (!is_const_json_array_v<V>)
            {
                return invalid_schema(pb.s, "enum", "array of values");
            }
            else
            {
                const Json allowed = to_runtime(schema_value);
                const Array &values = allowed.as_array();
                for (std::size_t i = 0; i < values.size(); ++i)
                {
                    if (value == values[i])
                        return ok();
                }
                return schema_result::Err(
                    SchemaError(SchemaErrorKind::EnumMismatch, pb.s, "one of enum", value_repr(value), "enum"));
            }
        }

        /// @brief `const`: exact equality with the single schema value.
        template <typename V>
        [[nodiscard]] schema_result check_const(const V &schema_value, const Json &value, PathBuf &pb)
        {
            const Json expected = to_runtime(schema_value);
            if (value == expected)
                return ok();
            return schema_result::Err(
                SchemaError(SchemaErrorKind::ConstMismatch, pb.s, "const value", value_repr(value), "const"));
        }

        // ---- length / items bounds -----------------------------------------

        /// @brief Number of Unicode code points (bytes that are not UTF-8
        ///        continuation bytes); invalid UTF-8 is counted as best effort.
        [[nodiscard]] inline std::size_t utf8_code_point_length(std::string_view s) noexcept
        {
            std::size_t n = 0;
            for (char c : s)
            {
                if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
                    ++n;
            }
            return n;
        }

        /// @brief Read a non-negative integer keyword argument.
        /// @return false when the argument is not a non-negative ConstJsonInt.
        template <typename V>
        [[nodiscard]] bool non_negative_int_bound(const V &schema_value, std::int64_t &out) noexcept
        {
            if constexpr (std::is_same_v<std::decay_t<V>, ConstJsonInt>)
            {
                out = schema_value.v;
                return schema_value.v >= 0;
            }
            else
            {
                (void)schema_value;
                (void)out;
                return false;
            }
        }

        /// @brief `minLength`: code-point count >= N (strings only).
        template <typename V>
        [[nodiscard]] schema_result check_min_length(const V &schema_value, const Json &value, PathBuf &pb)
        {
            std::int64_t bound = 0;
            if (!non_negative_int_bound(schema_value, bound))
                return invalid_schema(pb.s, "minLength", "non-negative integer");
            if (!value.is_string())
                return ok(); // keyword outside its domain
            const std::size_t len = utf8_code_point_length(value.as_string());
            if (len >= static_cast<std::size_t>(bound))
                return ok();
            return schema_result::Err(SchemaError(SchemaErrorKind::TooShort, pb.s, "length>=" + std::to_string(bound),
                                                  std::to_string(len), "minLength"));
        }

        /// @brief `maxLength`: code-point count <= N (strings only).
        template <typename V>
        [[nodiscard]] schema_result check_max_length(const V &schema_value, const Json &value, PathBuf &pb)
        {
            std::int64_t bound = 0;
            if (!non_negative_int_bound(schema_value, bound))
                return invalid_schema(pb.s, "maxLength", "non-negative integer");
            if (!value.is_string())
                return ok();
            const std::size_t len = utf8_code_point_length(value.as_string());
            if (len <= static_cast<std::size_t>(bound))
                return ok();
            return schema_result::Err(SchemaError(SchemaErrorKind::TooLong, pb.s, "length<=" + std::to_string(bound),
                                                  std::to_string(len), "maxLength"));
        }

        /// @brief `minItems`: array size >= N (arrays only).
        template <typename V>
        [[nodiscard]] schema_result check_min_items(const V &schema_value, const Json &value, PathBuf &pb)
        {
            std::int64_t bound = 0;
            if (!non_negative_int_bound(schema_value, bound))
                return invalid_schema(pb.s, "minItems", "non-negative integer");
            if (!value.is_array())
                return ok();
            const std::size_t size = value.as_array().size();
            if (size >= static_cast<std::size_t>(bound))
                return ok();
            return schema_result::Err(SchemaError(SchemaErrorKind::TooShort, pb.s, "items>=" + std::to_string(bound),
                                                  std::to_string(size), "minItems"));
        }

        /// @brief `maxItems`: array size <= N (arrays only).
        template <typename V>
        [[nodiscard]] schema_result check_max_items(const V &schema_value, const Json &value, PathBuf &pb)
        {
            std::int64_t bound = 0;
            if (!non_negative_int_bound(schema_value, bound))
                return invalid_schema(pb.s, "maxItems", "non-negative integer");
            if (!value.is_array())
                return ok();
            const std::size_t size = value.as_array().size();
            if (size <= static_cast<std::size_t>(bound))
                return ok();
            return schema_result::Err(SchemaError(SchemaErrorKind::TooLong, pb.s, "items<=" + std::to_string(bound),
                                                  std::to_string(size), "maxItems"));
        }

        // ---- numeric bounds ------------------------------------------------

        /// @brief A numeric keyword bound (Integer or Floating schema value).
        struct NumericBound
        {
            bool is_integer = true; ///< true: compare through `i`; false: through `d`
            std::int64_t i = 0;     ///< Integer bound when is_integer
            double d = 0.0;         ///< Floating bound (also the widened Integer)
        };

        /// @brief Read a numeric keyword argument.
        /// @return false when the argument is neither ConstJsonInt nor
        ///         ConstJsonDouble (reported as InvalidSchema by the caller).
        template <typename V> [[nodiscard]] bool numeric_bound_of(const V &schema_value, NumericBound &out) noexcept
        {
            if constexpr (std::is_same_v<std::decay_t<V>, ConstJsonInt>)
            {
                out.is_integer = true;
                out.i = schema_value.v;
                out.d = static_cast<double>(schema_value.v);
                return true;
            }
            else if constexpr (std::is_same_v<std::decay_t<V>, ConstJsonDouble>)
            {
                out.is_integer = false;
                out.i = 0;
                out.d = schema_value.v;
                return true;
            }
            else
            {
                (void)schema_value;
                (void)out;
                return false;
            }
        }

        /// @brief Render a numeric bound for an error message.
        [[nodiscard]] inline std::string numeric_bound_text(const NumericBound &bound)
        {
            if (bound.is_integer)
                return std::to_string(bound.i);
            char buf[32];
            const auto r = std::to_chars(buf, buf + sizeof(buf), bound.d);
            return std::string(buf, r.ptr);
        }

        /// @brief value >= bound, preserving int64 precision when both are int.
        [[nodiscard]] inline bool above_or_equal(const Json &value, const NumericBound &bound) noexcept
        {
            if (value.is_int())
            {
                if (bound.is_integer)
                    return value.as_int() >= bound.i;
                return static_cast<double>(value.as_int()) >= bound.d;
            }
            if (bound.is_integer)
                return value.as_float() >= static_cast<double>(bound.i);
            return value.as_float() >= bound.d;
        }

        /// @brief value <= bound, preserving int64 precision when both are int.
        [[nodiscard]] inline bool below_or_equal(const Json &value, const NumericBound &bound) noexcept
        {
            if (value.is_int())
            {
                if (bound.is_integer)
                    return value.as_int() <= bound.i;
                return static_cast<double>(value.as_int()) <= bound.d;
            }
            if (bound.is_integer)
                return value.as_float() <= static_cast<double>(bound.i);
            return value.as_float() <= bound.d;
        }

        /// @brief `minimum`: value >= bound (inclusive, numbers only).
        template <typename V>
        [[nodiscard]] schema_result check_minimum(const V &schema_value, const Json &value, PathBuf &pb)
        {
            NumericBound bound{};
            if (!numeric_bound_of(schema_value, bound))
                return invalid_schema(pb.s, "minimum", "number");
            if (!value.is_number())
                return ok();
            if (above_or_equal(value, bound))
                return ok();
            return schema_result::Err(SchemaError(SchemaErrorKind::BelowMinimum, pb.s, ">=" + numeric_bound_text(bound),
                                                  value_repr(value), "minimum"));
        }

        /// @brief `maximum`: value <= bound (inclusive, numbers only).
        template <typename V>
        [[nodiscard]] schema_result check_maximum(const V &schema_value, const Json &value, PathBuf &pb)
        {
            NumericBound bound{};
            if (!numeric_bound_of(schema_value, bound))
                return invalid_schema(pb.s, "maximum", "number");
            if (!value.is_number())
                return ok();
            if (below_or_equal(value, bound))
                return ok();
            return schema_result::Err(SchemaError(SchemaErrorKind::AboveMaximum, pb.s, "<=" + numeric_bound_text(bound),
                                                  value_repr(value), "maximum"));
        }

        // ---- additionalProperties ------------------------------------------

        /// @brief True when @p key is a declared key of a `properties` object.
        template <typename PV> [[nodiscard]] bool properties_has_key(const PV &props, std::string_view key)
        {
            if constexpr (!is_const_json_object_v<PV>)
            {
                (void)props;
                (void)key;
                return false;
            }
            else
            {
                (void)props;
                (void)key;
                using Entries = std::decay_t<decltype(props.entries)>;
                constexpr std::size_t n = std::tuple_size_v<Entries>;
                bool found = false;
                [&]<std::size_t... I>(std::index_sequence<I...>)
                {
                    ((found = found || (std::get<I>(props.entries).key == key)), ...);
                }(std::make_index_sequence<n>{});
                return found;
            }
        }

        /// @brief True when @p key is declared in the node's `properties`.
        template <typename Parent> [[nodiscard]] bool is_declared_property(const Parent &parent, std::string_view key)
        {
            (void)parent;
            (void)key;
            using Entries = std::decay_t<decltype(parent.entries)>;
            constexpr std::size_t n = std::tuple_size_v<Entries>;
            bool found = false;
            [&]<std::size_t... I>(std::index_sequence<I...>)
            {
                ((found = found || (std::get<I>(parent.entries).key == "properties" &&
                                    properties_has_key(std::get<I>(parent.entries).value, key))),
                 ...);
            }(std::make_index_sequence<n>{});
            return found;
        }

        /// @brief `additionalProperties`: `false` rejects keys not declared in
        ///        the same node's `properties`; `true` (or an absent keyword)
        ///        allows them. Non-boolean arguments are InvalidSchema.
        template <typename Parent, typename APV>
        [[nodiscard]] schema_result check_additional_properties(const Parent &parent, const APV &ap_value,
                                                                const Json &value, PathBuf &pb)
        {
            if constexpr (!std::is_same_v<std::decay_t<APV>, ConstJsonBool>)
            {
                (void)parent;
                (void)ap_value;
                (void)value;
                return invalid_schema(pb.s, "additionalProperties", "boolean");
            }
            else
            {
                if (ap_value.v)
                    return ok(); // true: extra keys allowed
                if (!value.is_object())
                    return ok(); // keyword outside its domain

                const Object &obj = value.as_object();
                for (const auto &entry : obj)
                {
                    const std::string_view key = entry.first;
                    if (is_declared_property(parent, key))
                        continue;
                    const std::size_t mark = pb.push_key(key);
                    schema_result r =
                        schema_result::Err(SchemaError(SchemaErrorKind::UnexpectedProperty, pb.s, "declared property",
                                                       "unexpected", "additionalProperties"));
                    pb.rewind(mark);
                    return r;
                }
                return ok();
            }
        }

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
            if (keyword == "enum")
                return check_enum(schema_value, value, pb);
            if (keyword == "const")
                return check_const(schema_value, value, pb);
            if (keyword == "minLength")
                return check_min_length(schema_value, value, pb);
            if (keyword == "maxLength")
                return check_max_length(schema_value, value, pb);
            if (keyword == "minItems")
                return check_min_items(schema_value, value, pb);
            if (keyword == "maxItems")
                return check_max_items(schema_value, value, pb);
            if (keyword == "minimum")
                return check_minimum(schema_value, value, pb);
            if (keyword == "maximum")
                return check_maximum(schema_value, value, pb);
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
                                          // additionalProperties needs the
                                          // sibling `properties` of this node.
                                          if (entry.key == "additionalProperties")
                                              return check_additional_properties(schema, entry.value, value, pb);
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
