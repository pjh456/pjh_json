#ifndef INCLUDE_PJH_JSON_STD_INTEROP_HPP
#define INCLUDE_PJH_JSON_STD_INTEROP_HPP

#include <cstdint>
#include <memory_resource>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "json.hpp"

namespace pjh::json
{
    /**
     * @brief Owned, copyable, all-std runtime value tree — the std-side mirror
     *        of Json (and the owned counterpart of as_variant()).
     *
     * Alternatives mirror Json's tags with the two string storage tags
     * collapsed into one std::string (exactly as as_variant() does):
     *
     * | alternative        | Json tag(s)               |
     * |--------------------|---------------------------|
     * | std::monostate     | Null                      |
     * | bool               | Boolean                   |
     * | std::int64_t       | Integer                   |
     * | double             | Floating                  |
     * | std::string        | StringView / StringOwned  |
     * | StdValue::Array    | ArrayType                 |
     * | StdValue::Object   | ObjectType                |
     *
     * Unlike Json (move-only, arena-bound), a StdValue is freely copyable and
     * uses the global (std) heap — this is the optional copy-semantics escape
     * hatch: convert with to_std(), copy/store/compare in std containers,
     * convert back with from_std() when returning to the library.
     *
     * @note Object order is PRESERVED: Object is a vector of (key, value)
     *       pairs, not a std::map — the library's insertion-ordered Object
     *       round-trips byte-identically through dump (and a std::map view is
     *       one user-side loop away if wanted).
     */
    struct StdValue
    {
        using Array  = std::vector<StdValue>;
        using Object = std::vector<std::pair<std::string, StdValue>>;
        using Data   = std::variant<std::monostate, bool, std::int64_t, double,
                                    std::string, Array, Object>;

        Data data;

        /**
         * @brief Construct null (monostate) value
         * @note Explicit constructors (not aggregate init): prevents
         *       std::pair's default-ctor `explicit` specifier from probing
         *       StdValue{} through the recursive variant/vector path, which
         *       fails to complete pair<string, StdValue> under
         *       clang++/libstdc++.
         */
        StdValue() : data(std::monostate{}) {}
        /**
         * @brief Construct from an active alternative
         * @param d Variant holding the value (moved in)
         */
        StdValue(Data d) : data(std::move(d)) {}

        /** @brief Content equality (recursive std variant/vector/string ==) */
        [[nodiscard]] bool operator==(const StdValue &other) const;
    };

    /**
     * @brief Deep-copy a Json tree into an owned std value tree.
     * @param value Json tree (borrowed strings are copied out)
     * @return Independent StdValue on the global heap; valid after the source
     *         Document/Json is destroyed
     * @throws std::bad_alloc on allocation failure
     * @note Uses the global/std heap (std::allocator), NOT PMR: the result is
     *       not tied to any arena and needs no resource parameter.
     * @note Slots map by identity: Integer->int64_t, Floating->double (no
     *       widening; get<T>()'s conversion track is not used). The two
     *       string tags both yield std::string. NaN/Inf are copied as-is.
     * @note The owned counterpart of as_variant() (which aliases storage).
     */
    [[nodiscard]] StdValue to_std(const Json &value);

    /**
     * @brief Build an arena-backed Json tree from a std value tree.
     * @param value Source std value tree (may die after the call)
     * @param res   Memory resource for the result's strings, object keys and
     *              container nodes (default: global config resource; nullptr
     *              also falls back to it). Must outlive the returned Json.
     * @return Json tree whose strings are owned in res
     * @throws std::bad_alloc on allocation failure
     * @note Explicit OWNED semantics: every string (value and key) is copied
     *       into res, so the source can die at the end of the call. This is
     *       the sanctioned path for bringing std::string / std containers
     *       INTO the library; the rvalue-borrow poison still forbids
     *       the implicit borrow entries (Json(std::string&&) etc.).
     * @note The std counterpart of clone(): clone() deep-copies Json->Json in
     *       an arena; from_std() materialises a std value INTO an arena.
     */
    [[nodiscard]] Json from_std(
        const StdValue &value,
        std::pmr::memory_resource *res = Config::instance().resource());
}

#endif // INCLUDE_PJH_JSON_STD_INTEROP_HPP
