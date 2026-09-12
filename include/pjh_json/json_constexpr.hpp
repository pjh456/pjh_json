#ifndef INCLUDE_PJH_JSON_CONSTEXPR_HPP
#define INCLUDE_PJH_JSON_CONSTEXPR_HPP

#include "json.hpp"
#include "document.hpp"
#include "validate.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace pjh::json
{

    // ===================================================================
    // ConstJsonKind — compile-time JSON kind tag
    // ===================================================================

    /// @brief Discriminator for the compile-time JSON value family.
    ///
    /// Every ConstJson* type exposes a `static constexpr ConstJsonKind kind_v`
    /// so that generic code can query the kind without knowing the concrete
    /// type. Use `const_json_kind_v<V>` for the decayed-type variable form.
    enum class ConstJsonKind : std::uint8_t
    {
        Null,   ///< ConstJsonNull
        Bool,   ///< ConstJsonBool
        Int,    ///< ConstJsonInt
        Double, ///< ConstJsonDouble
        String, ///< ConstJsonStr
        Array,  ///< ConstJsonArray<Ts...>
        Object  ///< ConstJsonObject<Es...>
    };

    // ===================================================================
    // compile-time scalar value types — one type per JSON scalar kind
    // ===================================================================

    /// @brief Compile-time JSON null value.
    struct ConstJsonNull
    {
        /// @brief Compile-time kind tag.
        static constexpr ConstJsonKind kind_v = ConstJsonKind::Null;
    };

    /// @brief Compile-time JSON boolean value.
    struct ConstJsonBool
    {
        bool v; ///< The boolean value.

        /// @brief Compile-time kind tag.
        static constexpr ConstJsonKind kind_v = ConstJsonKind::Bool;
    };

    /// @brief Compile-time JSON integer value (int64_t).
    struct ConstJsonInt
    {
        int64_t v; ///< The integer value.

        /// @brief Compile-time kind tag.
        static constexpr ConstJsonKind kind_v = ConstJsonKind::Int;
    };

    /// @brief Compile-time JSON floating-point value (double).
    struct ConstJsonDouble
    {
        double v; ///< The double value.

        /// @brief Compile-time kind tag.
        static constexpr ConstJsonKind kind_v = ConstJsonKind::Double;
    };

    /// @brief Compile-time JSON string value (borrowed view, no copy).
    struct ConstJsonStr
    {
        std::string_view v; ///< The string view.

        /// @brief Compile-time kind tag.
        static constexpr ConstJsonKind kind_v = ConstJsonKind::String;
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

        /// @brief Compile-time kind tag.
        static constexpr ConstJsonKind kind_v = ConstJsonKind::Array;

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
        /// @brief The compile-time JSON type of the value.
        using value_type = V;

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

        /// @brief Compile-time kind tag.
        static constexpr ConstJsonKind kind_v = ConstJsonKind::Object;

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
                // decay_t: a container argument forwards as an rvalue reference
                // from the to_const_json identity overload (T&&), so the
                // deduced element type would otherwise become
                // ConstJsonArray<...>&& and disagree with the decayed tuple
                // std::make_tuple stores.
                return ConstJsonArray<
                    std::decay_t<decltype(to_const_json(std::forward<Ts>(args)))>...>{
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
            /// @throws ParseError if the source is grammatically valid but its
            ///         runtime parse still fails: a number out of double range
            ///         (magnitude outside the finite-double range; RFC 8259 §6
            ///         range limit), or a runtime Config knob
            ///         (strip_bom / strict_utf8) not modelled at compile time.
            Document to_document() const { return parse_copy(source); }

        private:
            friend struct ConstJson;
            constexpr ParseResult(std::string_view s, bool v) : source(s), valid(v) {}
        };

        /// @brief Validate a JSON string at compile time.
        ///
        /// Checks whether the input is syntactically valid JSON.  The result
        /// can be used to optionally construct a Document at runtime.
        /// @note `valid` is a GRAMMAR verdict only: the compile-time validator
        ///       does not model the runtime double range limit (a huge
        ///       magnitude such as 1e400 is `valid` but `to_document()`
        ///       throws ParseError), nor the strip_bom / strict_utf8 knobs
        ///       (std::atomic is not constexpr). All three divergences are
        ///       by design and pinned in tests/literal_test.cpp.
        ///
        /// @note The compile-time validator is grammar-strict: a leading
        /// UTF-8 BOM (EF BB BF) is not whitespace (validate.hpp skip_whitespace)
        /// and is rejected (valid == false). The runtime strip_bom knob
        /// cannot reach consteval (std::atomic is not constexpr); this path
        /// stays BOM-strict by construction.
        /// @note The compile-time validator is escape-strict (surrogate
        /// pairs) but raw-byte-lenient: bytes >= 0x20 inside
        /// strings pass. The runtime strict_utf8 knob cannot reach
        ///       consteval (std::atomic is not constexpr); this divergence is
        ///       by design and pinned in tests/literal_test.cpp.
        /// @note The compile-time verdict and the runtime parse are held to a
        ///       strict equivalence (modulo the documented divergences above)
        ///       by tests/differential_validation.cpp, which feeds one corpus
        ///       table to both paths.
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

    // ===================================================================
    // compile-time kind introspection
    // ===================================================================

    /// @brief The ConstJsonKind of a compile-time JSON value type.
    /// @tparam V A ConstJson* type (decayed internally).
    ///
    /// The type must expose a `static constexpr ConstJsonKind kind_v`; a
    /// non-ConstJson type is a hard error at the point of instantiation.
    template <typename V>
    inline constexpr ConstJsonKind const_json_kind_v = std::decay_t<V>::kind_v;

    /// @brief True when v is a compile-time JSON null.
    /// @tparam V A ConstJson* type (deduced).
    /// @param v The value to inspect.
    /// @return true for ConstJsonNull, false otherwise.
    template <typename V>
    [[nodiscard]] constexpr bool is_null(const V &) noexcept
    {
        return const_json_kind_v<V> == ConstJsonKind::Null;
    }

    /// @brief True when v is a compile-time JSON boolean.
    /// @tparam V A ConstJson* type (deduced).
    /// @param v The value to inspect.
    /// @return true for ConstJsonBool, false otherwise.
    template <typename V>
    [[nodiscard]] constexpr bool is_bool(const V &) noexcept
    {
        return const_json_kind_v<V> == ConstJsonKind::Bool;
    }

    /// @brief True when v is a compile-time JSON integer.
    /// @tparam V A ConstJson* type (deduced).
    /// @param v The value to inspect.
    /// @return true for ConstJsonInt, false otherwise.
    template <typename V>
    [[nodiscard]] constexpr bool is_int(const V &) noexcept
    {
        return const_json_kind_v<V> == ConstJsonKind::Int;
    }

    /// @brief True when v is a compile-time JSON double.
    /// @tparam V A ConstJson* type (deduced).
    /// @param v The value to inspect.
    /// @return true for ConstJsonDouble, false otherwise.
    template <typename V>
    [[nodiscard]] constexpr bool is_double(const V &) noexcept
    {
        return const_json_kind_v<V> == ConstJsonKind::Double;
    }

    /// @brief True when v is a compile-time JSON string.
    /// @tparam V A ConstJson* type (deduced).
    /// @param v The value to inspect.
    /// @return true for ConstJsonStr, false otherwise.
    template <typename V>
    [[nodiscard]] constexpr bool is_string(const V &) noexcept
    {
        return const_json_kind_v<V> == ConstJsonKind::String;
    }

    /// @brief True when v is a compile-time JSON array.
    /// @tparam V A ConstJson* type (deduced).
    /// @param v The value to inspect.
    /// @return true for ConstJsonArray, false otherwise.
    template <typename V>
    [[nodiscard]] constexpr bool is_array(const V &) noexcept
    {
        return const_json_kind_v<V> == ConstJsonKind::Array;
    }

    /// @brief True when v is a compile-time JSON object.
    /// @tparam V A ConstJson* type (deduced).
    /// @param v The value to inspect.
    /// @return true for ConstJsonObject, false otherwise.
    template <typename V>
    [[nodiscard]] constexpr bool is_object(const V &) noexcept
    {
        return const_json_kind_v<V> == ConstJsonKind::Object;
    }

    // ===================================================================
    // scalar accessors — the argument type selects the accessor
    // ===================================================================

    /// @brief Read the value of a compile-time JSON boolean.
    /// @param v The boolean value.
    /// @return The stored bool.
    [[nodiscard]] constexpr bool as_bool(const ConstJsonBool &v) noexcept
    {
        return v.v;
    }

    /// @brief Read the value of a compile-time JSON integer.
    /// @param v The integer value.
    /// @return The stored int64_t.
    [[nodiscard]] constexpr std::int64_t as_int(const ConstJsonInt &v) noexcept
    {
        return v.v;
    }

    /// @brief Read the value of a compile-time JSON double.
    /// @param v The double value.
    /// @return The stored double.
    [[nodiscard]] constexpr double as_double(const ConstJsonDouble &v) noexcept
    {
        return v.v;
    }

    /// @brief Read the value of a compile-time JSON string.
    /// @param v The string value.
    /// @return The borrowed string view.
    [[nodiscard]] constexpr std::string_view as_string(const ConstJsonStr &v) noexcept
    {
        return v.v;
    }

    // ===================================================================
    // array access — get<I>() is compile-time indexed
    // ===================================================================

    /// @brief Access the I-th element of a compile-time JSON array.
    /// @tparam I The element index (must be < size()).
    /// @tparam Ts The array element types.
    /// @param a The array.
    /// @return A reference to the I-th element.
    template <std::size_t I, typename... Ts>
    [[nodiscard]] constexpr decltype(auto) get(const ConstJsonArray<Ts...> &a) noexcept
    {
        return std::get<I>(a.elems);
    }

    /// @brief The first element of a non-empty compile-time JSON array.
    /// @tparam Ts The array element types.
    /// @param a The array (must not be empty).
    /// @return A reference to element 0.
    template <typename... Ts>
        requires(sizeof...(Ts) > 0)
    [[nodiscard]] constexpr decltype(auto) front(const ConstJsonArray<Ts...> &a) noexcept
    {
        return std::get<0>(a.elems);
    }

    // ===================================================================
    // object access — positional get<I>() plus key lookup
    // ===================================================================

    /// @brief Access the I-th entry of a compile-time JSON object.
    /// @tparam I The entry index (must be < size()).
    /// @tparam Es The object entry types.
    /// @param o The object.
    /// @return A reference to the I-th ConstJsonEntry.
    template <std::size_t I, typename... Es>
    [[nodiscard]] constexpr decltype(auto) get(const ConstJsonObject<Es...> &o) noexcept
    {
        return std::get<I>(o.entries);
    }

    /// @brief Test whether a compile-time JSON object contains a key.
    /// @tparam Es The object entry types.
    /// @param o The object.
    /// @param k The key to look up.
    /// @return true when any entry's key equals k (structural order, no dedup).
    template <typename... Es>
    [[nodiscard]] constexpr bool has_key(const ConstJsonObject<Es...> &o,
                                         std::string_view k) noexcept
    {
        return [&]<std::size_t... I>(std::index_sequence<I...>)
        {
            return ((std::get<I>(o.entries).key == k) || ...);
        }(std::index_sequence_for<Es...>{});
    }

    namespace detail
    {
        /// @brief Return a pointer to the value of entry I when its wrapped
        ///        type matches T, else nullptr. Non-matching branches are
        ///        discarded with `if constexpr` (no ill-typed `&value`).
        template <std::size_t I, typename Tuple, typename T>
        [[nodiscard]] constexpr const T *const_json_find_one(const Tuple &t,
                                                             std::string_view k) noexcept
        {
            using E = std::tuple_element_t<I, Tuple>;
            if constexpr (std::is_same_v<typename E::value_type, T>)
                return std::get<I>(t).key == k ? &std::get<I>(t).value : nullptr;
            else
                return nullptr;
        }
    } // namespace detail

    /// @brief Find a key in a compile-time JSON object, typed by the wrapped
    ///        compile-time type (e.g. `find<ConstJsonInt>(o, "a")`).
    /// @tparam T The expected wrapped ConstJson type (must match exactly).
    /// @tparam Es The object entry types.
    /// @param o The object.
    /// @param k The key to look up.
    /// @return A pointer to the first structurally matching value of type T,
    ///         or nullptr when the key is missing or typed differently.
    /// @note Structural-order first match; duplicate keys are unsupported
    ///       (ConstJsonObject does not deduplicate).
    template <typename T, typename... Es>
    [[nodiscard]] constexpr const T *find(const ConstJsonObject<Es...> &o,
                                          std::string_view k) noexcept
    {
        const T *r = nullptr;
        [&]<std::size_t... I>(std::index_sequence<I...>)
        {
            ((r = r ? r : detail::const_json_find_one<I, std::tuple<Es...>, T>(o.entries, k)), ...);
        }(std::index_sequence_for<Es...>{});
        return r;
    }

    // ===================================================================
    // const_json_has_double — recursive compile-time double detector
    // ===================================================================

    /// @brief Trait: true when a compile-time JSON type contains a double
    ///        anywhere in its tree (the type itself or a nested element).
    ///
    /// consteval dump cannot format a double in C++20 (std::to_chars has no
    /// constexpr floating-point overload), so the dump entry points reject
    /// any tree for which this trait is true.
    /// @tparam V A ConstJson* type.
    template <typename V>
    struct const_json_has_double : std::false_type
    {
    };

    /// @brief Specialization: a double value is a double.
    template <>
    struct const_json_has_double<ConstJsonDouble> : std::true_type
    {
    };

    /// @brief Specialization: an array has a double iff any element does.
    template <typename... Ts>
    struct const_json_has_double<ConstJsonArray<Ts...>>
        : std::bool_constant<(const_json_has_double<std::decay_t<Ts>>::value || ...)>
    {
    };

    /// @brief Specialization: an object has a double iff any entry value does.
    template <typename... Es>
    struct const_json_has_double<ConstJsonObject<Es...>>
        : std::bool_constant<(const_json_has_double<typename Es::value_type>::value || ...)>
    {
    };

    /// @brief Convenience variable template for const_json_has_double
    ///        (decays the queried type first).
    /// @tparam V A ConstJson* type.
    template <typename V>
    inline constexpr bool const_json_has_double_v =
        const_json_has_double<std::decay_t<V>>::value;

    // ===================================================================
    // consteval dump — single traversal, two sinks
    // ===================================================================

    namespace detail
    {
        /// @brief Trait: true when T (decayed) is a ConstJsonArray.
        template <typename T>
        struct is_const_json_array : std::false_type
        {
        };

        /// @brief Specialization for ConstJsonArray.
        template <typename... Ts>
        struct is_const_json_array<ConstJsonArray<Ts...>> : std::true_type
        {
        };

        /// @brief Convenience variable template for is_const_json_array.
        template <typename T>
        inline constexpr bool is_const_json_array_v =
            is_const_json_array<std::decay_t<T>>::value;

        /// @brief Trait: true when T (decayed) is a ConstJsonObject.
        template <typename T>
        struct is_const_json_object : std::false_type
        {
        };

        /// @brief Specialization for ConstJsonObject.
        template <typename... Es>
        struct is_const_json_object<ConstJsonObject<Es...>> : std::true_type
        {
        };

        /// @brief Convenience variable template for is_const_json_object.
        template <typename T>
        inline constexpr bool is_const_json_object_v =
            is_const_json_object<std::decay_t<T>>::value;

        /// @brief Dependent-false helper for static_assert in discarded
        ///        `if constexpr` branches.
        template <typename>
        inline constexpr bool const_json_dependent_false = false;

        /// @brief Sink that counts the dumped byte length and discards content.
        struct const_json_count_sink
        {
            std::size_t n = 0; ///< Accumulated byte count.

            /// @brief Count one byte.
            constexpr void put(char) noexcept { ++n; }

            /// @brief Count every byte of a literal fragment.
            constexpr void put(std::string_view s) noexcept { n += s.size(); }
        };

        /// @brief Sink that writes into `[p, end)` and flags overflow instead
        ///        of ever writing out of bounds.
        struct const_json_buf_sink
        {
            char *p;                  ///< Current write position.
            char *end;                ///< One past the last writable byte.
            std::size_t written = 0;  ///< Bytes actually written.
            bool overflow = false;    ///< Set when a write was dropped.

            /// @brief Write one byte, or set overflow when full.
            constexpr void put(char c) noexcept
            {
                if (p != end)
                {
                    *p++ = c;
                    ++written;
                }
                else
                {
                    overflow = true;
                }
            }

            /// @brief Write every byte of a literal fragment.
            constexpr void put(std::string_view s) noexcept
            {
                for (char c : s)
                    put(c);
            }
        };

        /// @brief Lower-case hex digits, matching the runtime writer table.
        inline constexpr char const_json_hex_lower[] = "0123456789abcdef";

        /// @brief Single-traversal dump dispatcher. Declared first so the
        ///        container helpers below can recurse into it.
        template <typename Sink, typename V>
        constexpr void const_dump_to(Sink &sink, const V &v) noexcept;

        /// @brief Emit an int64 in minimal decimal form (INT64_MIN-safe).
        ///        Byte-identical to std::to_chars integer output.
        template <typename Sink>
        constexpr void const_dump_int(Sink &sink, std::int64_t v) noexcept
        {
            const bool neg = v < 0;
            // -(v + 1) + 1 avoids UB on INT64_MIN (unlike -v).
            std::uint64_t m = neg
                                  ? static_cast<std::uint64_t>(-(v + 1)) + 1
                                  : static_cast<std::uint64_t>(v);
            char buf[20];
            std::size_t n = 0;
            do
            {
                buf[n++] = static_cast<char>('0' + static_cast<int>(m % 10));
                m /= 10;
            } while (m != 0);
            if (neg)
                sink.put('-');
            while (n != 0)
                sink.put(buf[--n]);
        }

        /// @brief Emit a JSON string with surrounding quotes and the runtime
        ///        writer's default escaping (named escapes for the seven
        ///        specials, lower-case \u00xx for other controls, raw bytes
        ///        >= 0x20 including UTF-8 passthrough).
        template <typename Sink>
        constexpr void const_dump_string(Sink &sink, std::string_view str) noexcept
        {
            sink.put('"');
            for (char c : str)
            {
                switch (c)
                {
                case '"':
                    sink.put("\\\"");
                    break;
                case '\\':
                    sink.put("\\\\");
                    break;
                case '\b':
                    sink.put("\\b");
                    break;
                case '\f':
                    sink.put("\\f");
                    break;
                case '\n':
                    sink.put("\\n");
                    break;
                case '\r':
                    sink.put("\\r");
                    break;
                case '\t':
                    sink.put("\\t");
                    break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20)
                    {
                        const unsigned char u = static_cast<unsigned char>(c);
                        const char esc[6] = {
                            '\\', 'u', '0', '0',
                            const_json_hex_lower[(u >> 4) & 0xF],
                            const_json_hex_lower[u & 0xF]};
                        sink.put(std::string_view(esc, 6));
                    }
                    else
                    {
                        sink.put(c);
                    }
                    break;
                }
            }
            sink.put('"');
        }

        /// @brief Emit the elements of a compile-time array.
        template <typename Sink, typename... Ts, std::size_t... I>
        constexpr void const_dump_array_elems(Sink &sink, const ConstJsonArray<Ts...> &a,
                                              std::index_sequence<I...>) noexcept
        {
            sink.put('[');
            (
                ((void)((I != 0) && (sink.put(','), true)),
                 const_dump_to(sink, std::get<I>(a.elems))),
                ...);
            sink.put(']');
        }

        /// @brief Emit a complete compile-time array.
        template <typename Sink, typename... Ts>
        constexpr void const_dump_array(Sink &sink, const ConstJsonArray<Ts...> &a) noexcept
        {
            const_dump_array_elems(sink, a, std::index_sequence_for<Ts...>{});
        }

        /// @brief Emit the entries of a compile-time object.
        template <typename Sink, typename... Es, std::size_t... I>
        constexpr void const_dump_object_entries(Sink &sink, const ConstJsonObject<Es...> &o,
                                                 std::index_sequence<I...>) noexcept
        {
            sink.put('{');
            (
                ((void)((I != 0) && (sink.put(','), true)),
                 (const_dump_string(sink, std::get<I>(o.entries).key), sink.put(':'),
                  const_dump_to(sink, std::get<I>(o.entries).value))),
                ...);
            sink.put('}');
        }

        /// @brief Emit a complete compile-time object.
        template <typename Sink, typename... Es>
        constexpr void const_dump_object(Sink &sink, const ConstJsonObject<Es...> &o) noexcept
        {
            const_dump_object_entries(sink, o, std::index_sequence_for<Es...>{});
        }

        /// @brief Dispatch one value into the sink. Uses std::index_sequence +
        ///        fold (never std::apply) for consteval compatibility.
        template <typename Sink, typename V>
        constexpr void const_dump_to(Sink &sink, const V &v) noexcept
        {
            using T = std::decay_t<V>;
            if constexpr (std::is_same_v<T, ConstJsonNull>)
            {
                sink.put("null");
            }
            else if constexpr (std::is_same_v<T, ConstJsonBool>)
            {
                sink.put(v.v ? "true" : "false");
            }
            else if constexpr (std::is_same_v<T, ConstJsonInt>)
            {
                const_dump_int(sink, v.v);
            }
            else if constexpr (std::is_same_v<T, ConstJsonStr>)
            {
                const_dump_string(sink, v.v);
            }
            else if constexpr (is_const_json_array_v<T>)
            {
                const_dump_array(sink, v);
            }
            else if constexpr (is_const_json_object_v<T>)
            {
                const_dump_object(sink, v);
            }
            else if constexpr (std::is_same_v<T, ConstJsonDouble>)
            {
                static_assert(const_json_dependent_false<T>,
                              "consteval dump cannot format double in C++20: "
                              "std::to_chars has no constexpr floating-point overload; "
                              "use to_runtime() + runtime dump()");
            }
            else
            {
                static_assert(const_json_dependent_false<T>,
                              "const_dump: unsupported ConstJson value type");
            }
        }
    } // namespace detail

    /// @brief Exact compact-dump length of a compile-time JSON value, in bytes
    ///        (no trailing NUL). Never instantiates for a tree containing a
    ///        double: that is a compile-time error in C++20.
    /// @tparam V A ConstJson* type without a double.
    /// @param v The value to measure.
    /// @return Number of bytes const_dump() would write.
    template <typename V>
        requires requires { std::decay_t<V>::kind_v; }
    [[nodiscard]] constexpr std::size_t const_dump_size(const V &v) noexcept
    {
        static_assert(!const_json_has_double_v<V>,
                      "consteval dump cannot format double in C++20: "
                      "std::to_chars has no constexpr floating-point overload; "
                      "use to_runtime() + runtime dump()");
        detail::const_json_count_sink sink;
        detail::const_dump_to(sink, v);
        return sink.n;
    }

    /// @brief Write the compact dump of a compile-time JSON value into a
    ///        caller buffer `[out, out + cap)`. Writes no trailing NUL and
    ///        never writes out of bounds.
    /// @tparam V A ConstJson* type without a double.
    /// @param v The value to serialize.
    /// @param out Destination buffer (may be nullptr only when cap == 0).
    /// @param cap Capacity of @p out in bytes.
    /// @return Bytes written, or 0 when the dump did not fit in @p cap.
    template <typename V>
        requires requires { std::decay_t<V>::kind_v; }
    [[nodiscard]] constexpr std::size_t const_dump(const V &v, char *out,
                                                  std::size_t cap) noexcept
    {
        static_assert(!const_json_has_double_v<V>,
                      "consteval dump cannot format double in C++20: "
                      "std::to_chars has no constexpr floating-point overload; "
                      "use to_runtime() + runtime dump()");
        detail::const_json_buf_sink sink{out, out + cap};
        detail::const_dump_to(sink, v);
        return sink.overflow ? 0 : sink.written;
    }

    /// @brief Fixed-capacity inline text holding a single consteval dump.
    /// @tparam Cap Byte capacity (at least 1).
    ///
    /// `data` is public and the type is a literal aggregate so a value can be
    /// produced by consteval `const_dump()` and inspected in `static_assert`.
    template <std::size_t Cap>
    struct ConstJsonText
    {
        static_assert(Cap > 0, "ConstJsonText capacity must be at least 1");

        char data[Cap]{};        ///< Raw bytes (not NUL-terminated).
        std::size_t size = 0;    ///< Number of valid bytes in data.
        bool overflow = false;   ///< True when the dump exceeded Cap.

        /// @brief Borrowed view of the valid bytes.
        /// @return A string_view over `[data, data + size)`.
        [[nodiscard]] constexpr std::string_view view() const noexcept
        {
            return {data, size};
        }
    };

    /// @brief Single-call consteval dump into a fixed-capacity shell.
    /// @tparam Cap Capacity of the returned text (default 256 bytes).
    /// @tparam V A ConstJson* type without a double.
    /// @param v The value to serialize.
    /// @return A ConstJsonText whose `view()` yields the compact JSON text,
    ///         with `overflow = true` when Cap was too small.
    template <std::size_t Cap = 256, typename V>
        requires requires { std::decay_t<V>::kind_v; }
    [[nodiscard]] consteval ConstJsonText<Cap> const_dump(const V &v)
    {
        static_assert(!const_json_has_double_v<V>,
                      "consteval dump cannot format double in C++20: "
                      "std::to_chars has no constexpr floating-point overload; "
                      "use to_runtime() + runtime dump()");
        ConstJsonText<Cap> t;
        const std::size_t n = const_dump(v, t.data, Cap);
        if (n == 0)
            t.overflow = true;
        else
            t.size = n;
        return t;
    }

} // namespace pjh::json

#endif
