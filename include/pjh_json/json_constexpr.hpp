#ifndef INCLUDE_PJH_JSON_CONSTEXPR_HPP
#define INCLUDE_PJH_JSON_CONSTEXPR_HPP

#include "json.hpp"
#include "document.hpp"
#include "validate.hpp"

#include <concepts>
#include <string_view>
#include <tuple>

namespace pjh::json
{

    // ===================================================================
    // compile-time scalar value types — one type per JSON scalar kind
    // ===================================================================

    /// @brief Compile-time JSON null value.
    struct ConstJsonNull
    {
    };

    /// @brief Compile-time JSON boolean value.
    struct ConstJsonBool
    {
        bool v; ///< The boolean value.
    };

    /// @brief Compile-time JSON integer value (int64_t).
    struct ConstJsonInt
    {
        int64_t v; ///< The integer value.
    };

    /// @brief Compile-time JSON floating-point value (double).
    struct ConstJsonDouble
    {
        double v; ///< The double value.
    };

    /// @brief Compile-time JSON string value (borrowed view, no copy).
    struct ConstJsonStr
    {
        std::string_view v; ///< The string view.
    };

    // ===================================================================
    // to_runtime() — materialize a compile-time value into PMR Json
    // ===================================================================

    /// @brief Convert a compile-time null to runtime Json.
    /// @param res Memory resource for the result (default: global config resource).
    /// @return Json representing null.
    inline Json to_runtime(const ConstJsonNull &, std::pmr::memory_resource * = nullptr)
    {
        return Json();
    }

    /// @brief Convert a compile-time boolean to runtime Json.
    /// @param v The compile-time boolean.
    /// @param res Memory resource for the result (default: global config resource).
    /// @return Json representing the boolean.
    inline Json to_runtime(const ConstJsonBool &v, std::pmr::memory_resource * = nullptr)
    {
        return Json(v.v);
    }

    /// @brief Convert a compile-time integer to runtime Json.
    /// @param v The compile-time integer.
    /// @param res Memory resource for the result (default: global config resource).
    /// @return Json representing the integer.
    inline Json to_runtime(const ConstJsonInt &v, std::pmr::memory_resource * = nullptr)
    {
        return Json(v.v);
    }

    /// @brief Convert a compile-time double to runtime Json.
    /// @param v The compile-time double.
    /// @param res Memory resource for the result (default: global config resource).
    /// @return Json representing the double.
    inline Json to_runtime(const ConstJsonDouble &v, std::pmr::memory_resource * = nullptr)
    {
        return Json(v.v);
    }

    /// @brief Convert a compile-time string to runtime Json.
    /// @param v The compile-time string (borrowed view, caller guarantees lifetime).
    /// @param res Memory resource for the result (default: global config resource).
    /// @return Json representing the string.
    inline Json to_runtime(const ConstJsonStr &v, std::pmr::memory_resource * = nullptr)
    {
        return Json(v.v);
    }

    /// @brief Convert any compile-time container type to runtime Json by
    ///        delegating to its member .to_runtime() method.
    /// @tparam T A type with a .to_runtime(resource*) member (e.g. ConstJsonArray, ConstJsonObject).
    /// @param v The compile-time container.
    /// @param res Memory resource for the result (default: global config resource).
    /// @return Json representing the container.
    template <typename T>
    auto to_runtime(const T &v, std::pmr::memory_resource *res = nullptr)
        -> decltype(v.to_runtime(res))
    {
        return v.to_runtime(res);
    }

    // ===================================================================
    // forward declarations
    // ===================================================================

    template <typename... Ts>
    struct ConstJsonArray;

    template <typename... Es>
    struct ConstJsonObject;

    template <typename V>
    struct ConstJsonEntry;

    // ===================================================================
    // trait: is_const_json_entry
    // ===================================================================

    /// @brief Trait to detect ConstJsonEntry specializations.
    /// @tparam T The type to check.
    template <typename T>
    struct is_const_json_entry : std::false_type
    {
    };

    /// @brief Specialization: true when T is ConstJsonEntry<V> for some V.
    /// @tparam V The entry's value type.
    template <typename V>
    struct is_const_json_entry<ConstJsonEntry<V>> : std::true_type
    {
    };

    /// @brief Convenience variable template for is_const_json_entry.
    /// @tparam T The type to check.
    template <typename T>
    constexpr bool is_const_json_entry_v = is_const_json_entry<T>::value;

    // ===================================================================
    // to_const_json — dispatch scalar wrappers and container identity
    // ===================================================================

    /// @brief Identity for already-wrapped compile-time JSON container types.
    ///        Detected via the presence of the constjson_tag member type.
    /// @tparam T A type with ::constjson_tag (ConstJsonArray, ConstJsonObject).
    /// @param v The compile-time JSON container.
    /// @return The same object, forwarded as-is.
    template <typename T>
        requires requires { typename std::decay_t<T>::constjson_tag; }
    consteval T &&to_const_json(T &&v)
    {
        return static_cast<T &&>(v);
    }

    /// @brief Wrap nullptr as ConstJsonNull.
    /// @return ConstJsonNull{}.
    consteval ConstJsonNull to_const_json(std::nullptr_t) { return {}; }

    /// @brief Wrap bool as ConstJsonBool.
    /// @param v The boolean value.
    /// @return ConstJsonBool{v}.
    consteval ConstJsonBool to_const_json(bool v) { return {v}; }

    /// @brief Wrap int64_t as ConstJsonInt.
    /// @param v The integer value.
    /// @return ConstJsonInt{v}.
    consteval ConstJsonInt to_const_json(int64_t v) { return {v}; }

    /// @brief Wrap double as ConstJsonDouble.
    /// @param v The floating-point value.
    /// @return ConstJsonDouble{v}.
    consteval ConstJsonDouble to_const_json(double v) { return {v}; }

    /// @brief Wrap string_view as ConstJsonStr.
    /// @param v The string view.
    /// @return ConstJsonStr{v}.
    consteval ConstJsonStr to_const_json(std::string_view v) { return {v}; }

    /// @brief Wrap const char* as ConstJsonStr.
    /// @param v The C-string.
    /// @return ConstJsonStr{v}.
    consteval ConstJsonStr to_const_json(const char *v) { return {v}; }

    /// @brief Wrap any integral type (except bool) as ConstJsonInt.
    /// @tparam T An integral type (int, long, etc.).
    /// @param v The value.
    /// @return ConstJsonInt{static_cast<int64_t>(v)}.
    template <std::integral T>
        requires(!std::same_as<T, bool>)
    consteval ConstJsonInt to_const_json(T v)
    {
        return {static_cast<int64_t>(v)};
    }

    /// @brief Wrap any floating-point type as ConstJsonDouble.
    /// @tparam T A floating-point type (float, double, etc.).
    /// @param v The value.
    /// @return ConstJsonDouble{static_cast<double>(v)}.
    template <std::floating_point T>
    consteval ConstJsonDouble to_const_json(T v)
    {
        return {static_cast<double>(v)};
    }

    // ===================================================================
    // ConstJsonArray<Elements...> — compile-time JSON array
    // ===================================================================

    /// @brief Compile-time JSON array. Owns its elements inline in a std::tuple.
    /// @tparam Ts The types of the array elements (each a compile-time JSON type).
    template <typename... Ts>
    struct ConstJsonArray
    {
        /// @brief Tag to identify this as a compile-time JSON container type.
        using constjson_tag = void;

        /// @brief The array elements, stored inline in a tuple.
        std::tuple<Ts...> elems;

        /// @brief Compile-time element count.
        static constexpr size_t size_v = sizeof...(Ts);

        /// @brief Number of elements.
        /// @return The element count.
        constexpr size_t size() const noexcept { return size_v; }

        /// @brief Recursively materialize the entire array into PMR-backed Json.
        /// @param res Memory resource for allocations (default: global config resource).
        /// @return A Json Array containing all elements converted to runtime Json.
        Json to_runtime(std::pmr::memory_resource *res = nullptr) const
        {
            if (!res)
                res = Config::instance().resource();
            Array arr(res);
            arr.reserve(size_v);
            std::apply(
                [&](const auto &...e)
                { (arr.push_back(pjh::json::to_runtime(e, res)), ...); },
                elems);
            return Json(std::move(arr));
        }
    };

    // ===================================================================
    // ConstJsonEntry<Value> — compile-time key-value pair
    // ===================================================================

    /// @brief Compile-time key-value pair for object construction.
    /// @tparam V The compile-time JSON type of the value.
    template <typename V>
    struct ConstJsonEntry
    {
        /// @brief The object key (borrowed view into a string literal).
        std::string_view key;

        /// @brief The value (any compile-time JSON type).
        V value;
    };

    // ===================================================================
    // kv() — construct a compile-time key-value pair
    // ===================================================================

    /// @brief Create a ConstJsonEntry from a key and value. The value is
    ///        automatically converted to its compile-time JSON representation
    ///        via to_const_json().
    /// @param key The object key (string literal).
    /// @param val The value (scalar, array, or object).
    /// @return ConstJsonEntry with the key and the wrapped value.
    consteval auto kv(std::string_view key, auto val)
        -> ConstJsonEntry<std::decay_t<decltype(to_const_json(std::move(val)))>>
    {
        return {key, to_const_json(std::move(val))};
    }

    // ===================================================================
    // ConstJsonObject<Entries...> — compile-time JSON object
    // ===================================================================

    /// @brief Compile-time JSON object. Owns its entries inline in a std::tuple.
    /// @tparam Es The types of the object entries (each a ConstJsonEntry<V>).
    template <typename... Es>
    struct ConstJsonObject
    {
        /// @brief Tag to identify this as a compile-time JSON container type.
        using constjson_tag = void;

        /// @brief The object entries, stored inline in a tuple.
        std::tuple<Es...> entries;

        /// @brief Compile-time entry count.
        static constexpr size_t size_v = sizeof...(Es);

        /// @brief Number of entries.
        /// @return The entry count.
        constexpr size_t size() const noexcept { return size_v; }

        /// @brief Recursively materialize the entire object into PMR-backed Json.
        /// @param res Memory resource for allocations (default: global config resource).
        /// @return A Json Object containing all entries converted to runtime Json.
        Json to_runtime(std::pmr::memory_resource *res = nullptr) const
        {
            if (!res)
                res = Config::instance().resource();
            Object obj(res);
            std::apply(
                [&](const auto &...e)
                { (obj.insert(e.key, pjh::json::to_runtime(e.value, res)), ...); },
                entries);
            return Json(std::move(obj));
        }
    };

    // ===================================================================
    // ConstJson — static factory and parser entry point
    // ===================================================================

    /// @brief Static factory and parser for compile-time JSON.
    ///
    /// Use ConstJson::of() to construct compile-time JSON from C++ literals,
    /// and ConstJson::parse() to validate a JSON string at compile time.
    struct ConstJson
    {
        /// @name Factory methods
        /// @{

        /// @brief Construct a compile-time JSON object from key-value pairs.
        /// @tparam Ts Types of the arguments (all must be ConstJsonEntry<V>).
        /// @param args Key-value pairs created via kv().
        /// @return A ConstJsonObject containing the entries.
        template <typename... Ts>
            requires(sizeof...(Ts) > 0 &&
                     (is_const_json_entry_v<std::decay_t<Ts>> && ...))
        static consteval auto of(Ts &&...args)
        {
            return ConstJsonObject<std::decay_t<Ts>...>{
                std::make_tuple(std::forward<Ts>(args)...)};
        }

        /// @brief Construct a compile-time JSON array from scalar values or
        ///        containers.
        /// @tparam Ts Types of the arguments (must not be ConstJsonEntry).
        /// @param args Values to store in the array (scalars are auto-wrapped).
        /// @return A ConstJsonArray containing the elements.
        template <typename... Ts>
            requires(sizeof...(Ts) == 0 ||
                     (!(is_const_json_entry_v<std::decay_t<Ts>> || ...)))
        static consteval auto of(Ts &&...args)
        {
            if constexpr (sizeof...(Ts) == 0)
                return ConstJsonArray<>{std::tuple<>()};
            else
                return ConstJsonArray<decltype(to_const_json(std::forward<Ts>(args)))...>{
                    std::make_tuple(to_const_json(std::forward<Ts>(args))...)};
        }

        /// @}

        /// @brief Result of compile-time JSON validation.
        struct ParseResult
        {
            /// @brief The original JSON source string.
            std::string_view source;

            /// @brief Whether the source is valid JSON.
            bool valid;

            /// @brief Parse the validated source into a Document at runtime.
            /// @return A Document containing the parsed JSON tree.
            Document to_document() const { return parse_copy(source); }

        private:
            friend struct ConstJson;
            constexpr ParseResult(std::string_view s, bool v) : source(s), valid(v) {}
        };

        /// @brief Validate a JSON string at compile time.
        ///
        /// Checks whether the input is syntactically valid JSON.  The result
        /// can be used to optionally construct a Document at runtime without
        /// risk of parse errors.
        ///
        /// @param json The JSON string to validate.
        /// @return A ParseResult containing the source and validity flag.
        static consteval ParseResult parse(std::string_view json)
        {
            const char *p = json.data(), *e = p + json.size();
            bool ok = validate::value(p, e);
            validate::skip_whitespace(p, e);
            ok = ok && (p == e);
            return {json, ok};
        }
    };

} // namespace pjh::json

#endif
