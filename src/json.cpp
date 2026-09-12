#include "pjh_json/json.hpp"

#include <algorithm> // std::min / std::sort
#include <bit>       // std::bit_cast
#include <cmath>     // std::isnan
#include <vector>    // std::vector (object canonical sort buffer)

namespace pjh::json
{
    // --- task 26 TU-local helpers (operator< + std::hash payload) ---
    namespace
    {
        /* splitmix64: the 3-line finalizer (bijective). */
        uint64_t splitmix64(uint64_t x)
        {
            x += 0x9e3779b97f4a7c15ULL;
            x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
            x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
            return x ^ (x >> 31);
        }

        /* FNV-1a 64 over byte content (strings, object keys). */
        uint64_t fnv1a64(std::string_view s)
        {
            uint64_t h = 0xcbf29ce484222325ULL;
            for (unsigned char c : s)
                h = (h ^ static_cast<uint64_t>(c)) * 0x100000001b3ULL;
            return h;
        }

        /*
         * Class rank: Null < Boolean < Integer < Floating < String <
         * Array < Object. The two string tags share rank 4 (the
         * cross-tag fast path decides strings by content).
         */
        uint8_t rank(Json::Type t)
        {
            switch (t)
            {
            case Json::Type::Null:        return 0;
            case Json::Type::Boolean:     return 1;
            case Json::Type::Integer:     return 2;
            case Json::Type::Floating:    return 3;
            case Json::Type::StringView:
            case Json::Type::StringOwned: return 4;
            case Json::Type::ArrayType:   return 5;
            case Json::Type::ObjectType:  return 6;
            }
            return 0; // unreachable: all 8 tags covered above
        }

        /*
         * double < with NaN canonicalized to the maximum: raw double
         * comparison with NaN in play is not a strict weak ordering
         * (irreflexivity/transitivity break).
         */
        bool float_less(double a, double b)
        {
            if (std::isnan(a))
                return false;
            if (std::isnan(b))
                return true;
            return a < b;
        }

        /* Element-lexicographic array order (shorter prefix first). */
        bool array_less(const Array &a, const Array &b)
        {
            const size_t n = std::min(a.size(), b.size());
            for (size_t i = 0; i < n; ++i)
            {
                if (a[i] < b[i])
                    return true;
                if (b[i] < a[i])
                    return false;
            }
            return a.size() < b.size();
        }

        /*
         * Canonical key-sorted object order. The key comparator is the
         * SAME content predicate as Object::operator=='s find_slot
         * lookup (string_view content+length), so the two share one key
         * domain — CHANGE ONE, CHANGE THE OTHER. Pairs compare (key,
         * then value); shorter canonical prefix first. Duplicate-key
         * objects are outside the contract (see Object::operator==
         * @warning) and compare by the full sorted multiset.
         */
        bool object_less(const Object &a, const Object &b)
        {
            std::vector<const Object::Entry *> pa, pb;
            pa.reserve(a.size());
            pb.reserve(b.size());
            for (const auto &e : a.data())
                pa.push_back(&e);
            for (const auto &e : b.data())
                pb.push_back(&e);
            auto by_key = [](const Object::Entry *x, const Object::Entry *y) {
                return static_cast<std::string_view>(x->first) <
                       static_cast<std::string_view>(y->first);
            };
            std::sort(pa.begin(), pa.end(), by_key);
            std::sort(pb.begin(), pb.end(), by_key);
            const size_t n = std::min(pa.size(), pb.size());
            for (size_t i = 0; i < n; ++i)
            {
                if (static_cast<std::string_view>(pa[i]->first) !=
                    static_cast<std::string_view>(pb[i]->first))
                    return static_cast<std::string_view>(pa[i]->first) <
                           static_cast<std::string_view>(pb[i]->first);
                if (pa[i]->second < pb[i]->second)
                    return true;
                if (pb[i]->second < pa[i]->second)
                    return false;
            }
            return pa.size() < pb.size();
        }

        /*
         * Content hash: rank seed + payload mix; array fold ordered
         * (Array::== is order-dependent), object fold commutative ADD
         * (Object::== is order-independent — CHANGE ONE, CHANGE THE
         * OTHER). -0.0 folds to +0.0's bit pattern (0.0 == -0.0 must
         * hash equal); NaN hashes its bits (no equal pair involves NaN).
         * Duplicate-key objects are outside the contract (the hash folds
         * every entry, == resolves first-occurrence).
         */
        uint64_t hash64(const Json &j)
        {
            if (j.is_null())
                return splitmix64(0);
            if (j.is_boolean())
                return splitmix64(1) ^ (j.as_boolean() ? 1ULL : 0ULL);
            if (j.is_int())
                return splitmix64(2) ^ static_cast<uint64_t>(j.as_int());
            if (j.is_float())
            {
                uint64_t bits = std::bit_cast<uint64_t>(j.as_float());
                if (bits == 0x8000000000000000ULL) // -0.0 folds to +0.0
                    bits = 0;
                return splitmix64(3) ^ bits;
            }
            if (j.is_string())
                return splitmix64(4) ^ fnv1a64(j.as_string());
            if (j.is_array())
            {
                uint64_t h = splitmix64(5);
                for (const Json &e : j.as_array())
                    h = splitmix64(h ^ hash64(e));
                return h;
            }
            uint64_t h = splitmix64(6);
            for (const auto &kv : j.as_object().data())
                h += splitmix64(fnv1a64(static_cast<std::string_view>(kv.first)) ^
                                (hash64(kv.second) + 0x9e3779b97f4a7c15ULL));
            return h;
        }
    }

    // --- operator= ---

    /*
     * Assign string_view (borrowed)
     *
     * 1. Destroy old value.
     * 2. Store {ptr, len} inline as StringView.
     */
    Json &Json::operator=(std::string_view val)
    {
        destroy();
        m_type = Type::StringView;
        m_data.str_view.data = val.data();
        m_data.str_view.length = val.size();
        return *this;
    }

    /*
     * Assign C string (delegates to string_view overload)
     */
    Json &Json::operator=(const char *val)
    {
        return operator=(std::string_view(val));
    }

    /*
     * Assign array (takes ownership, heap-allocated via PMR)
     *
     * 1. Destroy old value.
     * 2. Allocate and construct Array on heap using its own m_resource.
     * 3. Store pointer as ArrayType.
     */
    Json &Json::operator=(Array &&arr)
    {
        destroy();
        m_data.heap = heap_alloc(arr.m_resource, std::move(arr));
        m_type = Type::ArrayType;
        return *this;
    }

    /*
     * Assign object (takes ownership, heap-allocated via PMR)
     *
     * 1. Destroy old value.
     * 2. Allocate and construct Object on heap using its own m_resource.
     * 3. Store pointer as ObjectType.
     */
    Json &Json::operator=(Object &&obj)
    {
        destroy();
        m_data.heap = heap_alloc(obj.m_resource, std::move(obj));
        m_type = Type::ObjectType;
        return *this;
    }

    // --- as_variant ---

    /*
     * Visitor dispatch: 8-way switch over m_type into a variant of
     * std::reference_wrapper payload aliases.
     *
     * 1. Raw references are not valid variant alternatives
     *    ([variant.requirements]: non-array object types only); the
     *    reference_wrapper<T> spellings carry the same aliasing semantics
     *    (extract the referent with .get()).
     * 2. StringView and StringOwned both yield std::string_view (the
     *    user-facing "string" is one type, same as as_string()). The two
     *    case labels stay stacked — 8-way = every tag covered, not
     *    case-label count (clone() shape, :83-84).
     * 3. The alternatives alias *this — the returned variant is valid only
     *    while *this is alive and unmutated (doxygen @warning).
     * 4. Exhaustive case list; the trailing return is unreachable
     *    (clone() idiom, :111).
     */
    std::variant<std::monostate,
                 std::reference_wrapper<const bool>,
                 std::reference_wrapper<const int64_t>,
                 std::reference_wrapper<const double>,
                 std::string_view,
                 std::reference_wrapper<const Array>,
                 std::reference_wrapper<const Object>>
    Json::as_variant() const noexcept
    {
        switch (m_type)
        {
        case Type::Null:
            return std::monostate{};
        case Type::Boolean:
            return m_data.boolean;
        case Type::Integer:
            return m_data.integer;
        case Type::Floating:
            return m_data.floating;
        case Type::StringView:
        case Type::StringOwned:
            return as_string();
        case Type::ArrayType:
            return *static_cast<const Array *>(m_data.heap);
        case Type::ObjectType:
            return *static_cast<const Object *>(m_data.heap);
        }
        return std::monostate{};
    }

    std::variant<std::monostate,
                 std::reference_wrapper<bool>,
                 std::reference_wrapper<int64_t>,
                 std::reference_wrapper<double>,
                 std::string_view,
                 std::reference_wrapper<Array>,
                 std::reference_wrapper<Object>>
    Json::as_variant() noexcept
    {
        switch (m_type)
        {
        case Type::Null:
            return std::monostate{};
        case Type::Boolean:
            return m_data.boolean;
        case Type::Integer:
            return m_data.integer;
        case Type::Floating:
            return m_data.floating;
        case Type::StringView:
        case Type::StringOwned:
            return as_string();
        case Type::ArrayType:
            return *static_cast<Array *>(m_data.heap);
        case Type::ObjectType:
            return *static_cast<Object *>(m_data.heap);
        }
        return std::monostate{};
    }

    // --- clone ---

    /*
     * Deep copy into target memory resource
     *
     * 1. Scalars (null/boolean/integer/floating): copy by value.
     * 2. String (borrowed or owned): read string_view, allocate owned
     *    pmr::string copy in target resource, store as StringOwned.
     * 3. Array/Object: delegate to container's clone() which recursively
     *    clones all children, then heap-allocate via PMR.
     */
    Json Json::clone(std::pmr::memory_resource *into) const
    {
        if (!into)
            into = Config::instance().resource();
        switch (m_type)
        {
        case Type::Null:
            return nullptr;
        case Type::Boolean:
            return m_data.boolean;
        case Type::Integer:
            return m_data.integer;
        case Type::Floating:
            return m_data.floating;
        case Type::StringView:
        case Type::StringOwned:
        {
            // Allocate/tag ordering (mirrors the Array/Object arms below):
            // make_owned is the only throwing call, so it must complete
            // before `out` exists. Tagging `out` first would unwind into
            // ~Json -> destroy_owned on a never-assigned m_data.heap.
            std::string_view sv = as_string();
            auto *ptr = String::make_owned(sv, into);
            Json out;
            out.m_type = Type::StringOwned;
            out.m_data.heap = ptr;
            return out;
        }
        case Type::ArrayType:
        {
            auto *ptr = heap_alloc(into, as_array().clone(into));
            Json out;
            out.m_type = Type::ArrayType;
            out.m_data.heap = ptr;
            return out;
        }
        case Type::ObjectType:
        {
            auto *ptr = heap_alloc(into, as_object().clone(into));
            Json out;
            out.m_type = Type::ObjectType;
            out.m_data.heap = ptr;
            return out;
        }
        }
        return nullptr;
    }

    // --- try_as ---

    /*
     * Safe access: check internal type, return nullopt/nullptr on mismatch.
     *
     * 1. Scalar types: compare m_type, return the value or nullopt.
     * 2. Array/Object: return pointer to heap object or nullptr.
     * 3. String: delegates to is_string() + as_string().
     */

    std::optional<bool> Json::try_as_boolean() const noexcept
    {
        if (m_type == Type::Boolean)
            return m_data.boolean;
        return std::nullopt;
    }

    std::optional<int64_t> Json::try_as_int() const noexcept
    {
        if (m_type == Type::Integer)
            return m_data.integer;
        return std::nullopt;
    }

    std::optional<double> Json::try_as_float() const noexcept
    {
        if (m_type == Type::Floating)
            return m_data.floating;
        return std::nullopt;
    }

    std::optional<std::string_view> Json::try_as_string() const noexcept
    {
        if (is_string())
            return as_string();
        return std::nullopt;
    }

    Array *Json::try_as_array() noexcept
    {
        if (m_type == Type::ArrayType)
            return static_cast<Array *>(m_data.heap);
        return nullptr;
    }

    const Array *Json::try_as_array() const noexcept
    {
        if (m_type == Type::ArrayType)
            return static_cast<const Array *>(m_data.heap);
        return nullptr;
    }

    Object *Json::try_as_object() noexcept
    {
        if (m_type == Type::ObjectType)
            return static_cast<Object *>(m_data.heap);
        return nullptr;
    }

    const Object *Json::try_as_object() const noexcept
    {
        if (m_type == Type::ObjectType)
            return static_cast<const Object *>(m_data.heap);
        return nullptr;
    }

    // --- size / empty ---

    /*
     * Delegate to contained container if array/object, else scalar
     *
     * 1. Array/object returns container size.
     * 2. Scalar always returns size=1, empty=false.
     */
    size_t Json::size() const noexcept
    {
        if (is_array())
            return as_array().size();
        if (is_object())
            return as_object().size();
        return 1;
    }

    bool Json::empty() const noexcept
    {
        if (is_array())
            return as_array().empty();
        if (is_object())
            return as_object().empty();
        return false;
    }

    // --- operator[] / at ---

    /*
     * Element access with type validation
     *
     * 1. Verify m_type is ArrayType or ObjectType (throw TypeError on mismatch).
     * 2. Delegate to the underlying container's accessor.
     *
     * operator[] skips bounds check (Array) or insert-if-missing (Object).
     * at() includes bounds check from the container.
     *
     * @throws TypeError if not the expected container type
     */

    Json &Json::operator[](size_t idx)
    {
        if (!is_array())
            throw TypeError("expected array");
        return as_array()[idx];
    }

    const Json &Json::operator[](size_t idx) const
    {
        if (!is_array())
            throw TypeError("expected array");
        return as_array()[idx];
    }

    Json &Json::at(size_t idx)
    {
        if (!is_array())
            throw TypeError("expected array");
        return as_array().at(idx);
    }

    const Json &Json::at(size_t idx) const
    {
        if (!is_array())
            throw TypeError("expected array");
        return as_array().at(idx);
    }

    Json &Json::operator[](std::string_view key)
    {
        if (!is_object())
            throw TypeError("expected object");
        return as_object()[key];
    }

    const Json &Json::operator[](std::string_view key) const
    {
        if (!is_object())
            throw TypeError("expected object");
        return as_object()[key];
    }

    Json &Json::at(std::string_view key)
    {
        if (!is_object())
            throw TypeError("expected object");
        return as_object().at(key);
    }

    const Json &Json::at(std::string_view key) const
    {
        if (!is_object())
            throw TypeError("expected object");
        return as_object().at(key);
    }

    // --- begin/end/keys/values ---

    /*
     * Range-for dispatch: the container tag picks the iterator's underlying
     * storage. A scalar has no children — TypeError in both build modes
     * (the at()/operator[] family; deliberately NOT the as_* debug_check
     * track: a range-for on a scalar is a user error, not an unchecked
     * access). size() is a different contract (cardinality, scalar = 1).
     */
    JsonIterator Json::begin()
    {
        if (is_array())
            return JsonIterator(as_array().begin());
        if (is_object())
            return JsonIterator(as_object().begin());
        throw TypeError("expected array or object");
    }

    JsonIterator Json::end()
    {
        if (is_array())
            return JsonIterator(as_array().end());
        if (is_object())
            return JsonIterator(as_object().end());
        throw TypeError("expected array or object");
    }

    ConstJsonIterator Json::begin() const
    {
        if (is_array())
            return ConstJsonIterator(as_array().begin());
        if (is_object())
            return ConstJsonIterator(as_object().begin());
        throw TypeError("expected array or object");
    }

    ConstJsonIterator Json::end() const
    {
        if (is_array())
            return ConstJsonIterator(as_array().end());
        if (is_object())
            return ConstJsonIterator(as_object().end());
        throw TypeError("expected array or object");
    }

    KeysView Json::keys() const
    {
        if (!is_object())
            throw TypeError("expected object");
        return as_object().keys();
    }

    ValuesView Json::values()
    {
        if (!is_object())
            throw TypeError("expected object");
        return as_object().values();
    }

    ConstValuesView Json::values() const
    {
        if (!is_object())
            throw TypeError("expected object");
        return as_object().values();
    }

    // --- operator== ---

    /*
     * Equality comparison
     *
     * 1. Json-vs-Json: compare m_type first for fast rejection, then
     *    compare values by type.
     * 2. Json-vs-scalar: type-check first, then value comparison.
     * 3. StringView and StringOwned are compared by content (not storage).
     */
    bool Json::operator==(const Json &other) const
    {
        if (is_string() && other.is_string())
            return as_string() == other.as_string();
        if (m_type != other.m_type)
            return false;
        switch (m_type)
        {
        case Type::Null:
            return true;
        case Type::Boolean:
            return m_data.boolean == other.m_data.boolean;
        case Type::Integer:
            return m_data.integer == other.m_data.integer;
        case Type::Floating:
            return m_data.floating == other.m_data.floating;
        case Type::StringView: // handled by the fast path above; listed for -Wswitch
        case Type::StringOwned:
            return as_string() == other.as_string();
        case Type::ArrayType:
            return as_array() == other.as_array();
        case Type::ObjectType:
            return as_object() == other.as_object();
        }
        return false;
    }

    bool Json::operator==(std::nullptr_t) const noexcept { return is_null(); }
    bool Json::operator==(bool val) const noexcept { return is_boolean() && as_boolean() == val; }
    bool Json::operator==(int64_t val) const noexcept { return is_int() && as_int() == val; }
    bool Json::operator==(double val) const noexcept { return is_float() && as_float() == val; }
    bool Json::operator==(std::string_view val) const noexcept { return is_string() && as_string() == val; }
    bool Json::operator==(const char *val) const noexcept { return operator==(std::string_view(val)); }
    bool Json::operator==(const Array &val) const { return is_array() && as_array() == val; }
    bool Json::operator==(const Object &val) const { return is_object() && as_object() == val; }

    // --- operator< ---

    /*
     * Strict weak ordering (see the header doxygen).
     *
     * 1. String fast path FIRST, cross-tag — verbatim the operator==
     *    fast path, CHANGE ONE, CHANGE THE OTHER.
     * 2. Class rank decides cross-class.
     * 3. Same class: payload compare (bool/int raw, double via
     *    float_less (NaN = maximum), string byte-lex, array element-lex,
     *    object canonical key-sorted form).
     * Contract invariant (no duplicate keys): a == b <=> !(a<b) && !(b<a).
     */
    bool Json::operator<(const Json &other) const
    {
        if (is_string() && other.is_string())
            return as_string() < other.as_string();
        if (rank(m_type) != rank(other.m_type))
            return rank(m_type) < rank(other.m_type);
        switch (m_type)
        {
        case Type::Null:
            return false;
        case Type::Boolean:
            return m_data.boolean < other.m_data.boolean;
        case Type::Integer:
            return m_data.integer < other.m_data.integer;
        case Type::Floating:
            return float_less(m_data.floating, other.m_data.floating);
        case Type::StringView:
        case Type::StringOwned:
            return as_string() < other.as_string();
        case Type::ArrayType:
            return array_less(as_array(), other.as_array());
        case Type::ObjectType:
            return object_less(as_object(), other.as_object());
        }
        return false; // unreachable: all 8 tags covered above
    }
}

// The member definition must sit in a namespace enclosing std::hash, so it
// lives at global scope here (unlike the operator< section above). hash64 is
// the TU-local payload recipe in src/json.cpp, reachable via the unnamed
// namespace's implicit using-directive.
size_t std::hash<pjh::json::Json>::operator()(const pjh::json::Json &value) const
{
    return static_cast<size_t>(pjh::json::hash64(value));
}
