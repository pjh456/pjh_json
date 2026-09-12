#include <doctest/doctest.h>
#include <ostream>
#include <pjh_json/json.hpp>
#include <pjh_json/document.hpp>
#include <pjh_json/parser.hpp>
#include <pjh_json/path.hpp>
#include <pjh_json/writer.hpp>
#include <limits>
#include <memory_resource>
#include <functional>
#include <variant>
#include <cstring>
#include <string>
#include <string_view>

using namespace pjh::json;

namespace
{
    // Local counting resource (the library's src/counting_resource.hpp is
    // private — not on the test include path). Upstream MUST be a
    // make_unique'd pool resource: never unique_ptr<new_delete_resource>()
    // (that singleton would be deleted — UB).
    struct TestCountingResource : std::pmr::memory_resource
    {
        explicit TestCountingResource()
            : m_up(std::make_unique<std::pmr::unsynchronized_pool_resource>()) {}

        [[nodiscard]] long long outstanding() const noexcept { return m_outstanding; }
        [[nodiscard]] size_t last_bytes() const noexcept { return m_last_bytes; }
        /// Monotonic count of do_allocate calls (unlike outstanding(), not
        /// affected by later deallocations) — used to pin allocation counts
        /// of a sequence of operations.
        [[nodiscard]] long long allocations() const noexcept { return m_allocations; }

    protected:
        void *do_allocate(std::size_t n, std::size_t align) override
        {
            ++m_outstanding;
            ++m_allocations;
            m_last_bytes = n;
            return m_up->allocate(n, align);
        }
        void do_deallocate(void *p, std::size_t n, std::size_t align) override
        {
            --m_outstanding;
            m_up->deallocate(p, n, align);
        }
        bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override
        {
            return this == &other;
        }

    private:
        std::unique_ptr<std::pmr::memory_resource> m_up;
        long long m_outstanding = 0;
        long long m_allocations = 0;
        size_t m_last_bytes = 0;
    };

    // MSVC debug STL allocates a per-container _Container_proxy through the
    // container's allocator, even for an empty std::pmr::string. It is not a
    // leak: the proxy goes back when the container dies, so exact-count pins
    // add this constant. It stays zero on libstdc++/libc++, keeping the
    // header/buffer count pins exact there.
#if defined(_MSC_VER) && defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL > 0
    constexpr long long kContainerOverhead = 1;
#else
    constexpr long long kContainerOverhead = 0;
#endif

    // True iff f() throws TypeError (other exceptions propagate => case fails)
    template <typename F>
    bool threw_type_error(F &&f)
    {
        try
        {
            f();
        }
        catch (const TypeError &)
        {
            return true;
        }
        return false;
    }

    // Expression-SFINAE probes (task-19 lesson: never name a failed member
    // lookup bare — probe the whole call expression instead).
    template <class O, class K, class = void>
    struct insert_key_ok : std::false_type {};
    template <class O, class K>
    struct insert_key_ok<O, K, std::void_t<decltype(
        std::declval<O &>().insert(std::declval<K>(), std::declval<Json>()))>>
        : std::true_type {};

    template <class O, class K, class = void>
    struct subscript_key_ok : std::false_type {};
    template <class O, class K>
    struct subscript_key_ok<O, K, std::void_t<decltype(
        std::declval<O &>()[std::declval<K>()])>> : std::true_type {};

    template <class T, class = void>
    struct array_of_ok : std::false_type {};
    template <class T>
    struct array_of_ok<T, std::void_t<decltype(Array::of(std::declval<T>()))>>
        : std::true_type {};
}

TEST_CASE("Json: simple value") {
    Json null_val(nullptr);
    REQUIRE(null_val.is_null());

    Json bool_val(true);
    REQUIRE(bool_val.is_boolean());
    REQUIRE(bool_val.as_boolean());
    REQUIRE(bool_val == true);

    Json int_val((int64_t)12);
    REQUIRE(int_val.is_int());
    REQUIRE(int_val.as_int() == (int64_t)12);
    REQUIRE(int_val == (int64_t)12);

    Json float_val((double)1.2);
    REQUIRE(float_val.is_float());
    REQUIRE(float_val.as_float() == (double)1.2);
    REQUIRE(float_val == (double)1.2);

    Json str_val("str");
    REQUIRE(str_val.is_string());
    REQUIRE(str_val.as_string() == "str");
    REQUIRE(str_val == "str");
}

TEST_CASE("Json: array value") {
    auto arr1 = std::move(Json(Array{}));
    REQUIRE(arr1.empty());
    REQUIRE(arr1.is_array());
    REQUIRE(arr1.size() == 0);
    REQUIRE_THROWS_AS((void)arr1.at(0), std::out_of_range);

    auto arr2 = std::move(Json(Array::of(Json("pjh"), Json((int64_t)123))));
    REQUIRE(!arr2.empty());
    REQUIRE(arr1.is_array());
    REQUIRE(arr2.size() == 2);
    REQUIRE(arr2[0] == "pjh");
    REQUIRE(arr2[1] == (int64_t)123);

    auto arr3 = std::move(Json(Array::of(Json((int64_t)1))));
    REQUIRE(arr2 != arr3);

    REQUIRE_THROWS_AS(arr2.as_array().erase(5), std::out_of_range);
}

TEST_CASE("Json: object value") {
    auto obj1 = std::move(Json(Object{}));
    REQUIRE(obj1.empty());
    REQUIRE(obj1.is_object());
    REQUIRE(obj1.size() == 0);

    using E = Object::Entry;
    auto obj2 = Json(Object::of(E{"pjh", Json((int64_t)123)}, E{"123", Json("pjh")}));
    REQUIRE(!obj2.empty());
    REQUIRE(obj2.is_object());
    REQUIRE(obj2.size() == 2);
    REQUIRE(obj2["pjh"] == (int64_t)123);
    REQUIRE(obj2["123"] == "pjh");

    auto obj3 = Json(Object::of(E{"only", Json((int64_t)1)}));
    REQUIRE(obj2 != obj3);

    static_assert(std::convertible_to<Object::Entry, Object::Entry>);
    static_assert(!std::convertible_to<int, Object::Entry>);
}

TEST_CASE("Json: object upsert") {
    Object obj;
    obj.insert("k", Json((int64_t)1));
    REQUIRE(obj.size() == 1);
    obj.insert("k", Json((int64_t)2));
    REQUIRE(obj.size() == 1);
    REQUIRE(obj["k"] == (int64_t)2);
}

TEST_CASE("Json: object remove bool") {
    Object obj;
    obj.insert("k", Json((int64_t)1));
    REQUIRE(obj.remove("k") == true);
    REQUIRE(obj.remove("k") == false);
    REQUIRE(obj.size() == 0);
}

TEST_CASE("Json: object content equality") {
    Object a;
    a.insert("a", Json((int64_t)1));
    a.insert("b", Json((int64_t)2));

    Object b;
    b.insert("b", Json((int64_t)2));
    b.insert("a", Json((int64_t)1));
    REQUIRE(a == b);

    Object c;
    c.insert("a", Json((int64_t)1));
    c.insert("b", Json((int64_t)99));
    REQUIRE(a != c);
}

TEST_CASE("Json: try_as") {
    Json i = Json((int64_t)42);
    CHECK(i.try_as_int().has_value());
    REQUIRE(*i.try_as_int() == 42);
    REQUIRE(!i.try_as_float().has_value());
    REQUIRE(!i.try_as_boolean().has_value());
    REQUIRE(!i.try_as_string().has_value());
    REQUIRE(i.try_as_array() == nullptr);
    REQUIRE(i.try_as_object() == nullptr);

    Json arr = Json(Array::of(Json((int64_t)1)));
    REQUIRE(arr.try_as_array() != nullptr);
    REQUIRE(arr.try_as_array()->size() == 1);
    REQUIRE(arr.try_as_object() == nullptr);

    Object ox;
    ox.insert("x", Json((int64_t)0));
    Json obj = Json(std::move(ox));
    REQUIRE(obj.try_as_object() != nullptr);
    REQUIRE(obj.try_as_array() == nullptr);

    Json s = Json("hi");
    CHECK(s.try_as_string().has_value());
    REQUIRE(*s.try_as_string() == "hi");
    REQUIRE(!s.try_as_int().has_value());

    Json b = Json(true);
    CHECK(b.try_as_boolean().has_value());
    REQUIRE(*b.try_as_boolean() == true);

    Json f = Json(3.14);
    CHECK(f.try_as_float().has_value());
    REQUIRE(*f.try_as_float() == 3.14);

    Json n = Json(nullptr);
    REQUIRE(!n.try_as_int().has_value());
}

TEST_CASE("Json: clone") {
    Json cloned;
    {
        auto doc = parse_copy(R"({"name":"pjh","nums":[1,2,3]})");
        cloned = doc.root().clone();
        REQUIRE(cloned["name"] == "pjh");
    }

    REQUIRE(cloned.is_object());
    REQUIRE(cloned["name"] == "pjh");
    REQUIRE(cloned["nums"].size() == 3);
    REQUIRE(cloned["nums"][2] == (int64_t)3);
}

TEST_CASE("Json: clone null resource falls back to config") {
    static const char kLong[] = "clone-null-source-long-enough-to-exceed-sso-cap";
    // Object root with a borrowed string value: pre-fix the value's
    // String::make_owned(nullptr) dereferences a null polymorphic_allocator
    // (deterministic SEGV); post-fix it resolves the global Config resource.
    {
        auto doc = parse_copy(R"({"s":"clone-null-source-long-enough-to-exceed-sso-cap"})");
        Json c = doc.root().clone(nullptr);
        doc.reset();                       // prove independence from the source buffer
        REQUIRE(c.is_object());
        REQUIRE(c["s"] == std::string_view(kLong));
    }

    // Array::clone(nullptr): element string clone is the load-bearing path.
    Array a = Array::of(Json(kLong));
    Array ca = a.clone(nullptr);
    REQUIRE(ca.size() == 1);
    REQUIRE(ca[0].as_string() == std::string_view(kLong));

    // Object::clone(nullptr): owned key + value clone.
    Object o;
    o.insert("k-null", Json(kLong));
    Object co = o.clone(nullptr);
    REQUIRE(co.size() == 1);
    REQUIRE(co.at("k-null").as_string() == std::string_view(kLong));

    // Already-safe controls (existing fallbacks, must stay green).
    Json jo = Json::own(kLong, nullptr);
    REQUIRE(jo.as_string() == std::string_view(kLong));
    Json built(std::string_view(kLong), nullptr);
    REQUIRE(built.as_string() == std::string_view(kLong));
}

TEST_CASE("Object: owned key insert") {
    std::pmr::memory_resource *mr = std::pmr::new_delete_resource();
    Object obj;
    obj.insert("owned-key", Json((int64_t)7), Config::instance().resource());
    obj.insert("mr-key", Json((int64_t)8), mr);
    REQUIRE(obj.size() == 2);
    REQUIRE(obj.contains("owned-key"));
    REQUIRE(obj.at("owned-key") == (int64_t)7);
    REQUIRE(obj.at("mr-key") == (int64_t)8);
    REQUIRE(obj.begin()->first.is_owned());

    // Content equality across storage modes: ref keeps borrowed literal keys
    Object ref;
    ref.insert("owned-key", Json((int64_t)7));
    ref.insert("mr-key", Json((int64_t)8));
    REQUIRE(ref.begin()->first.is_owned() == false);
    REQUIRE(obj == ref);

    // Writer path over owned keys
    std::pmr::string out = dump(Json(std::move(obj)));
    REQUIRE(out.find("\"owned-key\":7") != std::pmr::string::npos);
    REQUIRE(out.find("\"mr-key\":8") != std::pmr::string::npos);

    // Overwrite keeps the old (owned) key, last value wins
    Object obj2;
    obj2.insert("k", Json((int64_t)1), mr);
    obj2.insert("k", Json((int64_t)2), mr);
    REQUIRE(obj2.size() == 1);
    REQUIRE(obj2.at("k") == (int64_t)2);
    REQUIRE(obj2.begin()->first.is_owned());
    REQUIRE(obj2.remove("k") == true);
}

TEST_CASE("Object: owned key outlives source") {
    // (a) Temporary key source dies at the end of the inner scope
    Object obj;
    {
        std::string tmp = "temp-key";
        obj.insert(std::string_view(tmp), Json((int64_t)1),
                   Config::instance().resource());
    }
    REQUIRE(obj.contains("temp-key"));
    REQUIRE(obj.at("temp-key") == (int64_t)1);
    REQUIRE(obj.begin()->first.is_owned());

    // (b) Cross-document key: source document's buffer is reset afterwards
    Document doc = parse_copy(R"({"dyn":"v"})");
    Object obj2;
    for (const auto &[k, v] : doc.root().as_object())
        obj2.insert(static_cast<std::string_view>(k), v.clone(),
                    Config::instance().resource());
    doc.reset();
    REQUIRE(obj2.contains("dyn"));
    REQUIRE(obj2.at("dyn") == "v");
}

// --- task 21.1: Object iterator key side is read-only ---
//
// Negative compile pins detect over the ASSIGNMENT expression (task-19
// lesson: never name a failed member lookup bare — that is a hard error;
// an assignment expression inside void_t is SFINAE-friendly). The RHS is
// String&& — the only viable mutation channel (String's copy-assign is
// deleted, string.hpp:117); a const String& RHS would false-negative on
// the regression tree (no candidate for String& = const String&).
template <class T, class = void>
struct key_side_assignable : std::false_type {};
template <class T>
struct key_side_assignable<T, std::void_t<decltype(
    std::declval<T &>().first = std::declval<String &&>())>>
    : std::true_type {};

template <class T, class = void>
struct key_arrow_assignable : std::false_type {};
template <class T>
struct key_arrow_assignable<T, std::void_t<decltype(
    std::declval<T &>()->first = std::declval<String &&>())>>
    : std::true_type {};

TEST_CASE("Object: iterator key side is read-only") {
    // It is begin()'s RETURN type (not the bare class), so a begin()
    // reverted to a raw Vec::iterator flips red too.
    using It = decltype(std::declval<Object &>().begin());
    // The prvalue type of operator* (the write path, non-const track).
    // NB: *declval<It*>() would be It& itself, not the yield type.
    using Yield = decltype((*std::declval<It &>()));
    // --- negative compile pins (flip to compile-RED on regression) ---
    static_assert(!key_side_assignable<Yield>::value,
                  "yield: the key side (first) must not be assignable");
    static_assert(!key_arrow_assignable<It>::value,
                  "iterator: ->first must not be assignable");
    // --- positive type pins ---
    static_assert(std::is_same_v<Yield, Object::EntryRef>);
    static_assert(std::is_same_v<decltype(std::declval<Yield &>().first), const String &>);
    static_assert(std::is_same_v<decltype(std::declval<Yield &>().second), Json &>);
    // --- const track: already sealed, recorded (not fixed by this task) ---
    using CIt = decltype(std::declval<const Object &>().begin());
    static_assert(std::is_same_v<CIt, Object::Vec::const_iterator>);
    static_assert(!key_side_assignable<decltype(*std::declval<CIt &>())>::value);

    // --- positive runtime pins (value side still patchable, reads unchanged) ---
    Object obj;
    obj.insert("a", Json((int64_t)1));
    obj.insert("b", Json((int64_t)2));
    for (auto &&kv : obj)                 // value patch through the new yield
        kv.second = Json((int64_t)9);
    REQUIRE(obj.at("a") == (int64_t)9);
    REQUIRE(obj.at("b") == (int64_t)9);
    REQUIRE(static_cast<std::string_view>(obj.begin()->first) == "a");
    REQUIRE(obj.begin()->first.is_owned() == false);   // borrowed literal, unchanged
    REQUIRE(obj.contains("a"));                        // find unaffected by iteration
    REQUIRE(obj.size() == 2);
    const Object &cobj = obj;
    int seen = 0;
    for (const auto &[k, v] : cobj)
    {
        REQUIRE(v == (int64_t)9);
        (void)k;
        ++seen;
    }
    REQUIRE(seen == 2);
}

TEST_CASE("Json: object parse-insert semantics unified") {
    // insert-on-existing and parse-duplicate-key must agree: last-wins
    Object o;
    o.insert("a", Json((int64_t)1));
    o.insert("a", Json((int64_t)2));
    REQUIRE(o.size() == 1);
    REQUIRE(o.at("a") == (int64_t)2);

    auto d = parse_copy(R"({"a":1,"a":2})");
    REQUIRE(d.root().size() == 1);
    REQUIRE(d.root()["a"] == (int64_t)2);
    REQUIRE(Json(std::move(o)) == d.root());
}

// Cross-resource move-assign: pmr allocators never propagate, so libstdc++
// element-wise moves into this container's own buffer; the moved-in node
// must stay bound to this container's resource, not the source's. The
// source arena is destroyed BEFORE ~j to pin the UAF (ASan-only red).
TEST_CASE("Array: cross-resource move assign") {
    auto resA = std::make_unique<std::pmr::unsynchronized_pool_resource>();
    auto resB = std::make_unique<std::pmr::unsynchronized_pool_resource>();
    Json j; // declared last so ~j runs while both pools are still alive
    {
        Array a(resA.get());
        a.push_back(Json((int64_t)42));
        a.push_back(Json((int64_t)7));
        Array b(resB.get());
        b = std::move(a);
        REQUIRE(b.size() == 2);
        REQUIRE(b[1] == (int64_t)7);
        REQUIRE(a.empty());
        // Storage ownership white-box pin: b's buffer lives in resB
        REQUIRE(b.data().get_allocator().resource() == resB.get());
        j = Json(std::move(b));
    }
    resA.reset(); // source arena dies before ~j
    REQUIRE(j.is_array());
    REQUIRE(j.size() == 2);
    REQUIRE(j[0] == (int64_t)42);
}

TEST_CASE("Object: cross-resource move assign") {
    auto resA = std::make_unique<std::pmr::unsynchronized_pool_resource>();
    auto resB = std::make_unique<std::pmr::unsynchronized_pool_resource>();
    Json j; // declared last so ~j runs while both pools are still alive
    {
        Object oA(resA.get());
        oA.insert("k", Json((int64_t)7)); // borrowed literal key
        Object oB(resB.get());
        oB = std::move(oA);
        REQUIRE(oB.size() == 1);
        REQUIRE(oB["k"] == (int64_t)7);
        REQUIRE(oA.empty());
        // Storage ownership white-box pin: oB's buffer lives in resB
        REQUIRE(oB.data().get_allocator().resource() == resB.get());
        j = Json(std::move(oB));
    }
    resA.reset(); // source arena dies before ~j
    REQUIRE(j.is_object());
    REQUIRE(j.size() == 1);
    REQUIRE(j["k"] == (int64_t)7);
}

// Control group: same-resource move assign must keep stealing the storage
// buffer zero-cost (buffer pointer unchanged) — guards against any future
// "unify on element-wise move" simplification regressing the hot path.
TEST_CASE("Array: same-resource move assign steals storage") {
    auto res = std::make_unique<std::pmr::unsynchronized_pool_resource>();
    Array a(res.get());
    a.reserve(8);
    a.push_back(Json((int64_t)1));
    Json *buf = a.data().data();
    Array b(res.get());
    b = std::move(a);
    REQUIRE(b.data().data() == buf);
    REQUIRE(a.empty());
    REQUIRE(b.size() == 1);
}

TEST_CASE("Object: same-resource move assign steals storage") {
    auto res = std::make_unique<std::pmr::unsynchronized_pool_resource>();
    Object oA(res.get());
    oA.insert("k", Json((int64_t)1));
    auto *buf = oA.data().data();
    Object oB(res.get());
    oB = std::move(oA);
    REQUIRE(oB.data().data() == buf);
    REQUIRE(oA.empty());
    REQUIRE(oB.size() == 1);
}

// Same-resource move assign used to end with the moved-from source's
// m_resource nulled (steal + assign-null), so adopting the moved-from
// container into a Json heap_alloc's through a null resource (UB: null
// deref in polymorphic_allocator::allocate). The moved-from container must
// stay bound to the shared resource: its node then allocates and frees
// through that same live resource. The move-ctor path is pinned in the same
// case (identical defect class, same one-line fix shape).
TEST_CASE("Array: same-res move assign keeps source adoptable") {
    auto res = std::make_unique<std::pmr::unsynchronized_pool_resource>();
    Json j;
    {
        Array a(res.get());
        a.push_back(Json((int64_t)42));
        a.push_back(Json((int64_t)7));
        Array b(res.get());
        b = std::move(a);
        REQUIRE(b.size() == 2);
        REQUIRE(b[0] == (int64_t)42);
        REQUIRE(a.empty());
        // moved-from allocator member stays bound to the shared resource
        REQUIRE(a.data().get_allocator().resource() == res.get());
        j = Json(std::move(a)); // pre-fix: heap_alloc(nullptr) -> UB
    }
    REQUIRE(j.is_array());
    REQUIRE(j.size() == 0);
    REQUIRE(j.empty());
    REQUIRE(dump(j) == "[]");

    {
        Array c(res.get());
        c.push_back(Json((int64_t)9));
        Array d(std::move(c));
        REQUIRE(d.size() == 1);
        REQUIRE(d[0] == (int64_t)9);
        REQUIRE(c.empty());
        REQUIRE(c.data().get_allocator().resource() == res.get());
        j = Json(std::move(c));
    }
    REQUIRE(j.is_array());
    REQUIRE(j.size() == 0);
    REQUIRE(dump(j) == "[]");
}

TEST_CASE("Object: same-res move assign keeps source adoptable") {
    auto res = std::make_unique<std::pmr::unsynchronized_pool_resource>();
    Json j;
    {
        Object oA(res.get());
        oA.insert("k", Json((int64_t)7)); // borrowed literal key
        Object oB(res.get());
        oB = std::move(oA);
        REQUIRE(oB.size() == 1);
        REQUIRE(oB["k"] == (int64_t)7);
        REQUIRE(oA.empty());
        // moved-from allocator member stays bound to the shared resource
        REQUIRE(oA.data().get_allocator().resource() == res.get());
        j = Json(std::move(oA)); // pre-fix: heap_alloc(nullptr) -> UB
    }
    REQUIRE(j.is_object());
    REQUIRE(j.size() == 0);
    REQUIRE(j.empty());
    REQUIRE(dump(j) == "{}");

    {
        Object oC(res.get());
        oC.insert("m", Json((int64_t)9));
        Object oD(std::move(oC));
        REQUIRE(oD.size() == 1);
        REQUIRE(oD["m"] == (int64_t)9);
        REQUIRE(oC.empty());
        REQUIRE(oC.data().get_allocator().resource() == res.get());
        j = Json(std::move(oC));
    }
    REQUIRE(j.is_object());
    REQUIRE(j.size() == 0);
    REQUIRE(j.empty());
    REQUIRE(dump(j) == "{}");
}

// --- task 50: threshold-gated Object lookup index ---
//
// The index is a private cache, so these cases pin the observable contract:
// N above the threshold exercises the indexed lookup / append / erase paths,
// while the small-object cases elsewhere pin that nothing changed below it.
// Keys are borrowed from `keys`, which is reserved up-front so SSO buffers
// never move underneath the borrowed views.

TEST_CASE("Object: hash index large lookup") {
    constexpr int N = 256;
    std::vector<std::string> keys;
    keys.reserve(N);
    for (int i = 0; i < N; ++i)
        keys.push_back("key_" + std::to_string(i));

    Object o;
    for (int i = 0; i < N; ++i)
    {
        if (i % 2 == 0)
            o[keys[i]] = Json((int64_t)i); // insert-if-missing path
        else
            o.insert(keys[i], Json((int64_t)i));
    }
    REQUIRE(o.size() == (size_t)N);

    for (int i = 0; i < N; ++i)
    {
        REQUIRE(o.contains(keys[i]));
        REQUIRE(o.at(keys[i]) == (int64_t)i);
        REQUIRE(static_cast<const Object &>(o).at(keys[i]) == (int64_t)i);
    }
    REQUIRE(!o.contains("not-a-key"));
    REQUIRE_THROWS_AS(o.at("not-a-key"), std::out_of_range);
    REQUIRE_THROWS_AS(static_cast<const Object &>(o).at("not-a-key"),
                      std::out_of_range);
    REQUIRE_THROWS_AS(static_cast<const Object &>(o)["not-a-key"],
                      std::out_of_range);

    // Insertion order is untouched by the index.
    std::vector<std::string> got;
    got.reserve(N);
    for (std::string_view k : o.keys())
        got.emplace_back(k);
    REQUIRE(got == keys);
}

TEST_CASE("Object: hash index overwrite and remove") {
    constexpr int N = 100;
    std::vector<std::string> keys;
    keys.reserve(N + 64);
    for (int i = 0; i < N; ++i)
        keys.push_back("k" + std::to_string(i));

    Object o;
    for (int i = 0; i < N; ++i)
        o.insert(keys[i], Json((int64_t)i));
    REQUIRE(o.size() == (size_t)N);

    // Overwrite keeps the first key/position (last-wins) and the size.
    for (int i = 0; i < N; i += 2)
        o.insert(keys[i], Json((int64_t)(i + 1000)));
    REQUIRE(o.size() == (size_t)N);
    for (int i = 0; i < N; i += 2)
        REQUIRE(o.at(keys[i]) == (int64_t)(i + 1000));

    // Remove rebuilds the index (positions shift).
    for (int i = 1; i < N; i += 2)
        REQUIRE(o.remove(keys[i]));
    REQUIRE(o.size() == (size_t)(N / 2));
    for (int i = 1; i < N; i += 2)
    {
        REQUIRE(!o.contains(keys[i]));
        REQUIRE_THROWS_AS(o.at(keys[i]), std::out_of_range);
    }
    for (int i = 0; i < N; i += 2)
        REQUIRE(o.at(keys[i]) == (int64_t)(i + 1000));

    // Re-inserting a removed key appends at the tail (order contract).
    o.insert(keys[1], Json((int64_t)777));
    REQUIRE(o.size() == (size_t)(N / 2 + 1));
    REQUIRE(o.at(keys[1]) == (int64_t)777);
    std::string last;
    for (std::string_view k : o.keys())
        last.assign(k);
    REQUIRE(last == keys[1]);

    // Remove down past the threshold: the index is dropped, lookups still
    // answer from the linear fallback.
    std::vector<std::string> present;
    for (std::string_view k : o.keys())
        present.emplace_back(k);
    for (const auto &k : present)
    {
        if (o.size() <= 8)
            break;
        REQUIRE(o.remove(k));
    }
    REQUIRE(o.size() <= 8);
    for (std::string_view k : o.keys())
        REQUIRE(o.contains(k));

    // Grow back above the threshold and re-exercise the rebuilt index.
    const size_t base = o.size();
    for (int i = 200; i < 240; ++i)
    {
        keys.push_back("z" + std::to_string(i));
        o.insert(keys.back(), Json((int64_t)i));
        REQUIRE(o.at(keys.back()) == (int64_t)i);
    }
    REQUIRE(o.size() == base + 40);
}

TEST_CASE("Object: hash index move assign") {
    constexpr int N = 64;
    std::vector<std::string> keys;
    keys.reserve(N);
    for (int i = 0; i < N; ++i)
        keys.push_back("m" + std::to_string(i));

    // Same resource: storage is stolen and the index travels with it.
    {
        auto res = std::make_unique<std::pmr::unsynchronized_pool_resource>();
        Object oA(res.get());
        for (int i = 0; i < N; ++i)
            oA.insert(keys[i], Json((int64_t)i));
        const Object &cA = oA;
        auto *buf = cA.data().data(); // const data() must not drop the index
        Object oB(res.get());
        oB = std::move(oA);
        REQUIRE(static_cast<const Object &>(oB).data().data() == buf);
        REQUIRE(oA.empty());
        REQUIRE(oB.size() == (size_t)N);
        for (int i = 0; i < N; ++i)
            REQUIRE(oB.at(keys[i]) == (int64_t)i);
        REQUIRE(!oB.contains("missing"));
    }

    // Cross resource: entries move into the target's resource; the target
    // stays correct (index lazily rebuilt by the next append) and the
    // source's index must not leak into the wrong resource.
    {
        auto resA = std::make_unique<std::pmr::unsynchronized_pool_resource>();
        auto resB = std::make_unique<std::pmr::unsynchronized_pool_resource>();
        Json keep; // declared last so ~keep runs while both pools are alive
        {
            Object oA(resA.get());
            for (int i = 0; i < N; ++i)
                oA.insert(keys[i], Json((int64_t)i));
            Object oB(resB.get());
            oB = std::move(oA);
            REQUIRE(oB.size() == (size_t)N);
            REQUIRE(oA.empty());
            REQUIRE(static_cast<const Object &>(oB).data().get_allocator().resource() ==
                    resB.get());
            for (int i = 0; i < N; ++i)
                REQUIRE(oB.at(keys[i]) == (int64_t)i);
            oB.insert("fresh", Json((int64_t)999));
            REQUIRE(oB.at("fresh") == (int64_t)999);
            for (int i = 0; i < N; ++i)
                REQUIRE(oB.at(keys[i]) == (int64_t)i);
            keep = Json(std::move(oB));
        }
        resA.reset(); // source arena dies before ~keep; target storage is in resB
        REQUIRE(keep.is_object());
        REQUIRE(keep.at("fresh") == (int64_t)999);
    }
}

TEST_CASE("Object: hash index clone") {
    constexpr int N = 64;
    std::vector<std::string> keys;
    keys.reserve(N);
    for (int i = 0; i < N; ++i)
        keys.push_back("c" + std::to_string(i));

    Object o;
    for (int i = 0; i < N; ++i)
        o.insert(keys[i], Json((int64_t)i));

    Object c = o.clone();
    REQUIRE(c.size() == o.size());
    for (int i = 0; i < N; ++i)
        REQUIRE(c.at(keys[i]) == (int64_t)i);
    REQUIRE(!c.contains("missing"));
    REQUIRE(o == c);
}

TEST_CASE("Object: hash index data() invalidation") {
    constexpr int N = 64;
    std::vector<std::string> keys;
    keys.reserve(N);
    for (int i = 0; i < N; ++i)
        keys.push_back("d" + std::to_string(i));

    Object o;
    for (int i = 0; i < N; ++i)
        o.insert(keys[i], Json((int64_t)i));
    REQUIRE(o.contains(keys[10]));

    // The mutable raw surface drops the cache but leaves the vector intact;
    // lookups fall back to the linear sweep and stay correct.
    Object::Vec &raw = o.data();
    REQUIRE(raw.size() == (size_t)N);
    for (int i = 0; i < N; ++i)
        REQUIRE(o.at(keys[i]) == (int64_t)i);

    // A value patched through the raw surface is visible to the fallback.
    raw[3].second = Json((int64_t)333);
    REQUIRE(o.at(keys[3]) == (int64_t)333);

    // The next append rebuilds the index; keys and patched value survive.
    o.insert("extra", Json((int64_t)1));
    REQUIRE(o.at("extra") == (int64_t)1);
    for (int i = 0; i < N; ++i)
        REQUIRE(o.at(keys[i]) == (int64_t)(i == 3 ? 333 : i));
    REQUIRE(o.at(keys[3]) == (int64_t)333);
}

TEST_CASE("Object: hash index clear and reuse") {
    constexpr int N = 64;
    std::vector<std::string> keys;
    keys.reserve(N);
    for (int i = 0; i < N; ++i)
        keys.push_back("e" + std::to_string(i));

    Object o;
    for (int i = 0; i < N; ++i)
        o.insert(keys[i], Json((int64_t)i));
    o.clear();
    REQUIRE(o.empty());
    REQUIRE(!o.contains(keys[0]));

    for (int i = 0; i < N; ++i)
        o.insert(keys[i], Json((int64_t)(i + 1)));
    REQUIRE(o.size() == (size_t)N);
    for (int i = 0; i < N; ++i)
        REQUIRE(o.at(keys[i]) == (int64_t)(i + 1));
}

TEST_CASE("Object: hash index accounting") {
    // Above the threshold the index buffer is allocated through the
    // object's own resource; destruction must return every byte.
    TestCountingResource cr;
    {
        Object o(&cr);
        for (int i = 0; i < 64; ++i)
        {
            std::string k = "acct" + std::to_string(i);
            o.insert(k, Json((int64_t)i), &cr);
        }
        REQUIRE(o.size() == 64);
        REQUIRE(o.at("acct63") == (int64_t)63);
        REQUIRE(cr.outstanding() > 0);
    }
    REQUIRE(cr.outstanding() == 0);
}

TEST_CASE("Object: hash index duplicate adoptee") {
    // An adopted vector may carry duplicate keys. The index must keep the
    // first occurrence (matching the old linear first-match sweep).
    std::vector<std::string> store;
    store.reserve(40);
    for (int i = 0; i < 40; ++i)
        store.emplace_back("dup");
    Object::Vec v;
    v.reserve(40);
    for (int i = 0; i < 40; ++i)
        v.emplace_back(String{std::string_view(store[static_cast<size_t>(i)])},
                       Json((int64_t)i));

    Object o(std::move(v));
    REQUIRE(o.size() == 40);
    REQUIRE(o.at("dup") == (int64_t)0); // no index yet: linear first-wins

    // A miss append builds the index; first-wins must survive the build.
    o.insert("other", Json((int64_t)9));
    REQUIRE(o.at("dup") == (int64_t)0);
    REQUIRE(o.at("other") == (int64_t)9);

    // remove erases the first occurrence; the next duplicate becomes first.
    REQUIRE(o.remove("dup"));
    REQUIRE(o.size() == 40);
    REQUIRE(o.at("dup") == (int64_t)1);
    REQUIRE(o.contains("other"));
}

// --- task 48: allocation-free Object equality + Array::contains noexcept ---

TEST_CASE("Object: content equality large order-insensitive") {
    constexpr int N = 256;
    std::vector<std::string> keys;
    keys.reserve(N);
    for (int i = 0; i < N; ++i)
        keys.push_back("eq_" + std::to_string(i));

    // Both sides are above kIndexThreshold, so insert materialises the
    // lookup index; two different insertion orders must compare equal.
    Object a;
    for (int i = 0; i < N; ++i)
        a.insert(keys[static_cast<size_t>(i)], Json((int64_t)i));

    Object b;
    for (int i = N - 1; i >= 0; --i)
        b.insert(keys[static_cast<size_t>(i)], Json((int64_t)i));

    REQUIRE(a == b);
    REQUIRE(b == a); // symmetric

    // Value mismatch.
    b.insert(keys[0], Json((int64_t)-1));
    REQUIRE(a != b);
    REQUIRE(b != a);
    b.insert(keys[0], Json((int64_t)0));
    REQUIRE(a == b);

    // Same size, different key set: one key misses on the other side.
    Object c;
    for (int i = 0; i < N; ++i)
        c.insert(keys[static_cast<size_t>(i)], Json((int64_t)i));
    c.remove(keys[1]);
    c.insert("eq_replaced", Json((int64_t)1));
    REQUIRE(c.size() == a.size());
    REQUIRE(a != c);

    // Size difference rejects before any lookup.
    c.remove("eq_replaced");
    REQUIRE(a != c);

    // Nested containers recurse order-insensitively through Json equality.
    Object inner1;
    inner1.insert("x", Json((int64_t)1));
    inner1.insert("y", Json((int64_t)2));
    Object inner2;
    inner2.insert("y", Json((int64_t)2));
    inner2.insert("x", Json((int64_t)1));
    Object n1;
    n1.insert("obj", Json(std::move(inner1)));
    n1.insert("arr", Json(Array::of(Json((int64_t)7), Json("s"))));
    Object n2;
    n2.insert("arr", Json(Array::of(Json((int64_t)7), Json("s"))));
    n2.insert("obj", Json(std::move(inner2)));
    REQUIRE(n1 == n2);
    REQUIRE(n2 == n1);
}

TEST_CASE("Object: content equality small and unindexed") {
    constexpr int N = 16; // exactly kIndexThreshold: no index is built
    std::vector<std::string> keys;
    keys.reserve(N);
    for (int i = 0; i < N; ++i)
        keys.push_back("sm_" + std::to_string(i));

    Object a;
    for (int i = 0; i < N; ++i)
        a.insert(keys[static_cast<size_t>(i)], Json((int64_t)i));
    Object b;
    for (int i = N - 1; i >= 0; --i)
        b.insert(keys[static_cast<size_t>(i)], Json((int64_t)i));
    REQUIRE(a == b);
    REQUIRE(b == a);

    // Object(Vec) adoption never materialises an index (both sides stay
    // unindexed): the linear fallback must still compare correctly.
    Object::Vec v;
    v.reserve(N);
    for (int i = 0; i < N; ++i)
        v.emplace_back(String{std::string_view(keys[static_cast<size_t>(i)])},
                       Json((int64_t)i));
    Object adopted(std::move(v));
    REQUIRE(a == adopted);
    REQUIRE(adopted == a);
    REQUIRE(b == adopted);

    // data() drops the index on a large object; both sides then sweep
    // linearly and a raw-patched value is visible to the comparison.
    std::vector<std::string> big_keys;
    big_keys.reserve(64);
    for (int i = 0; i < 64; ++i)
        big_keys.push_back("big_" + std::to_string(i));
    Object big1;
    Object big2;
    for (int i = 0; i < 64; ++i)
    {
        big1.insert(big_keys[static_cast<size_t>(i)], Json((int64_t)i));
        big2.insert(big_keys[static_cast<size_t>(i)], Json((int64_t)i));
    }
    (void)big1.data();
    (void)big2.data();
    REQUIRE(big1 == big2);
    big2.data()[5].second = Json((int64_t)-9);
    REQUIRE(big1 != big2);
}

TEST_CASE("Object: content equality large unindexed lazy index") {
    // Both sides are parse-shaped: built on the counting resource through
    // Object(Vec) adoption, so neither carries an index. The first
    // comparison must materialise the probed side's index once (a mutable,
    // amortised cache write); every later comparison of the pair must be
    // allocation-free. Executable guard against the both-unindexed O(n^2)
    // regression (R_48 F1): a revert to a pure linear sweep allocates
    // nothing on the first compare, and a per-comparison allocation would
    // keep growing the counter.
    TestCountingResource cr;
    constexpr int N = 64; // > kIndexThreshold
    std::vector<std::string> keys;
    keys.reserve(N);
    for (int i = 0; i < N; ++i)
        keys.push_back("lz_" + std::to_string(i));

    Object::Vec va{&cr};
    Object::Vec vb{&cr};
    va.reserve(N);
    vb.reserve(N);
    for (int i = 0; i < N; ++i)
        va.emplace_back(String{std::string_view(keys[static_cast<size_t>(i)])},
                        Json((int64_t)i));
    for (int i = N - 1; i >= 0; --i)
        vb.emplace_back(String{std::string_view(keys[static_cast<size_t>(i)])},
                        Json((int64_t)i));
    Object a(std::move(va));
    Object b(std::move(vb));

    const long long before = cr.allocations();
    REQUIRE(a == b); // lazily builds b's index (one amortised allocation)
    REQUIRE(cr.allocations() > before);
    const long long after_first = cr.allocations();
    for (int i = 0; i < 200; ++i)
    {
        REQUIRE(a == b);
        REQUIRE(b == a);
    }
    REQUIRE(cr.allocations() == after_first); // no per-comparison allocation
    REQUIRE(a == b);
    REQUIRE(b == a);
}

TEST_CASE("Array: contains element") {
    // Contract pins: Array::contains is not noexcept (comparison can
    // allocate), Object::contains stays noexcept (index lookup only).
    static_assert(!noexcept(std::declval<const Array &>().contains(
        std::declval<const Json &>())));
    static_assert(noexcept(std::declval<const Object &>().contains(
        std::declval<std::string_view>())));

    Array empty;
    REQUIRE(!empty.contains(Json((int64_t)1)));

    Array scalars = Array::of(Json((int64_t)1), Json("two"), Json(true));
    REQUIRE(scalars.contains(Json((int64_t)1)));
    REQUIRE(scalars.contains(Json("two")));
    REQUIRE(scalars.contains(Json(true)));
    REQUIRE(!scalars.contains(Json((int64_t)2)));
    REQUIRE(!scalars.contains(Json(false)));

    // A nested Object with a different insertion order must still be found:
    // Object::operator== is order-insensitive through Array::contains.
    Object o1;
    o1.insert("a", Json((int64_t)1));
    o1.insert("b", Json((int64_t)2));
    Array arr = Array::of(Json(std::move(o1)));

    Object probe;
    probe.insert("b", Json((int64_t)2));
    probe.insert("a", Json((int64_t)1));
    REQUIRE(arr.contains(Json(std::move(probe))));

    Object miss;
    miss.insert("a", Json((int64_t)1));
    miss.insert("b", Json((int64_t)99));
    REQUIRE(!arr.contains(Json(std::move(miss))));
}

TEST_CASE("Path: at_path success") {
    auto doc = parse_copy(R"({"a":{"b":[10,20,{"c":true}]}})");
    auto &root = doc.root();
    REQUIRE(root.at_path("a.b[2].c") == true);
    REQUIRE(root.at_path("a.b[0]") == (int64_t)10);
    REQUIRE(root.at_path("a.b[1]") == (int64_t)20);
    REQUIRE(root.at_path("a").is_object());
    REQUIRE(root.at_path("") == root); // empty path = the root itself

    // Bracket tail-chain over a real path in the doc
    auto d2 = parse_copy(R"({"m":[[1,2],[3,4]]})");
    REQUIRE(d2.root().at_path("m[1][0]") == (int64_t)3);

    // Const track: both const overloads must be callable (compile-time pin)
    const Json &cj = doc.root();
    REQUIRE(cj.at_path("a.b[1]") == (int64_t)20);
    REQUIRE(cj.at_path(Path{}) == cj);
}

TEST_CASE("Path: at_path throws per hop class") {
    auto doc = parse_copy(R"({"x":5})");
    auto &root = doc.root();
    REQUIRE_THROWS_AS((void)root.at_path("y"), std::out_of_range); // missing key
    REQUIRE_THROWS_AS((void)root.at_path("x.y"), TypeError); // scalar mid-walk, key hop
    REQUIRE_THROWS_AS((void)root.at_path("x[0]"), TypeError); // scalar mid-walk, index hop

    auto d2 = parse_copy(R"({"a":[1,2,3]})");
    REQUIRE_THROWS_AS((void)d2.root().at_path("a[9]"), std::out_of_range); // OOB index
    REQUIRE_THROWS_AS((void)d2.root().at_path("a.x"), std::out_of_range); // key on array, non-digit

    REQUIRE_THROWS_AS((void)root.at_path("a[3"), std::invalid_argument); // malformed DSL
}

TEST_CASE("Path: find_path found and miss") {
    auto doc = parse_copy(R"({"a":{"b":[10,20,{"c":true}]}})");
    auto &root = doc.root();
    REQUIRE(root.find_path("a.b[2].c") != nullptr);
    REQUIRE(*root.find_path("a.b[0]") == (int64_t)10);
    REQUIRE(root.find_path("a.b[9]") == nullptr); // OOB index
    REQUIRE(root.find_path("z") == nullptr); // missing key
    REQUIRE(root.find_path("a.b.x") == nullptr); // key on array, non-digit
    REQUIRE(root.find_path("") == &root); // empty path = this

    auto d2 = parse_copy(R"({"x":5})");
    REQUIRE(d2.root().find_path("x.y") == nullptr); // scalar mid-walk hop

    // sv and Path overloads resolve to the same node
    REQUIRE(root.find_path(parse_path("a.b[2].c")) == root.find_path("a.b[2].c"));

    // Const overload: const Json * (compile-time pin)
    const Json &cj = doc.root();
    const Json *p = cj.find_path("a.b[1]");
    REQUIRE(p != nullptr);
    REQUIRE(*p == (int64_t)20);
    REQUIRE(cj.find_path("a.q") == nullptr);

    // Malformed grammar penetrates — a parameter error, not a miss
    REQUIRE_THROWS_AS((void)root.find_path("a[3"), std::invalid_argument);
}

TEST_CASE("Path: contains predicate") {
    // Contract pin: the typed-path predicate is no longer noexcept — a hop
    // allocates (decimal key) and a miss constructs an exception message;
    // an OOM must propagate, not std::terminate.
    static_assert(!noexcept(std::declval<const Json &>().contains(
        std::declval<const Path &>())));

    auto doc = parse_copy(R"({"a":{"b":[10,20]}})");
    auto &root = doc.root();
    REQUIRE(root.contains("a.b[1]") == true);
    REQUIRE(root.contains("a.b[9]") == false); // OOB
    REQUIRE(root.contains("z") == false); // missing key
    REQUIRE(root.contains("") == true); // empty path always true
    REQUIRE(root.contains(Path{}) == true); // typed overload

    // Index step over an object parent => decimal-string key lookup; a miss
    // must yield false (and, after the fix, must not be sitting under noexcept).
    Path numeric_key;
    numeric_key.emplace_back((size_t)9);
    REQUIRE(root.contains(numeric_key) == false); // object has no key "9"

    // Wrong-type (scalar mid-walk) hop = false, not an error
    auto d2 = parse_copy(R"({"x":5})");
    REQUIRE(d2.root().contains("x.y") == false);
    REQUIRE(d2.root().contains("x[0]") == false);
    REQUIRE(d2.root().contains("x") == true);
}

TEST_CASE("Path: digit key parent-decides rule") {
    auto doc = parse_copy(R"({"o":{"3":1},"a":[1,2,3]})");
    auto &root = doc.root();
    REQUIRE(root.at_path("o[3]") == (int64_t)1); // object parent + bracket = key "3"
    REQUIRE(root.at_path("o.3") == (int64_t)1); // object parent + dot-digit = key "3"
    REQUIRE(root.at_path("a.1") == (int64_t)2); // array parent + dot-digit = index 1
    REQUIRE(root.at_path("a[1]") == (int64_t)2);
    REQUIRE(root.contains("a.x") == false); // array parent + non-digit dot segment
    REQUIRE_THROWS_AS((void)root.at_path("a.x"), std::out_of_range);

    // Both frontends share the rule: DSL-compiled vs hand-built Path
    REQUIRE(root.at_path(parse_path("o[3]")) == (int64_t)1);
    Path manual;
    manual.emplace_back(std::string_view{"o"});
    manual.emplace_back((size_t)3);
    REQUIRE(root.at_path(manual) == (int64_t)1);
}

TEST_CASE("Path: parse_path valid corpus") {
    auto p = parse_path("a.b[3].c");
    REQUIRE(p.size() == 4);
    REQUIRE(*std::get_if<std::string_view>(&p[0]) == "a");
    REQUIRE(*std::get_if<std::string_view>(&p[1]) == "b");
    REQUIRE(std::get<size_t>(p[2]) == 3);
    REQUIRE(*std::get_if<std::string_view>(&p[3]) == "c");

    REQUIRE(parse_path("").empty()); // empty = root

    auto p2 = parse_path("a[0][1]"); // bracket tail-chain
    REQUIRE(p2.size() == 3);
    REQUIRE(*std::get_if<std::string_view>(&p2[0]) == "a");
    REQUIRE(std::get<size_t>(p2[1]) == 0);
    REQUIRE(std::get<size_t>(p2[2]) == 1);

    auto p3 = parse_path("a..b"); // interior ".." = the empty key
    REQUIRE(p3.size() == 3);
    REQUIRE(*std::get_if<std::string_view>(&p3[0]) == "a");
    REQUIRE(*std::get_if<std::string_view>(&p3[1]) == "");
    REQUIRE(*std::get_if<std::string_view>(&p3[2]) == "b");

    auto p4 = parse_path("a[03]"); // leading zeros accepted
    REQUIRE(p4.size() == 2);
    REQUIRE(std::get<size_t>(p4[1]) == 3);

    auto p5 = parse_path("[5]"); // root-level index, empty key part
    REQUIRE(p5.size() == 1);
    REQUIRE(std::get<size_t>(p5[0]) == 5);
}

TEST_CASE("Path: parse_path invalid corpus") {
    const std::string_view bad[] = {
        "a[3", "a[]", "a[-1]", "a[1.5]", "a[0]b", ".a", "a.",
        "a[99999999999999999999]",
    };
    for (auto s : bad)
        REQUIRE_THROWS_AS((void)parse_path(s), std::invalid_argument);
}

TEST_CASE("Json: owned string ctor") {
    TestCountingResource cr;
    std::string src(48, 'a'); // > max SSO capacity (~23) => buffer must go through res
    std::string_view sv(src);
    {
        Json j(sv, &cr);
        REQUIRE(j.is_string());
        REQUIRE(j.as_string() == sv);
        // Header + buffer both go through res now (was buffer only):
        // 1 header + 1 buffer + optional MSVC _Container_proxy. The exact
        // count is the resource-routing pin; last_bytes() is only a soft
        // size sanity check (the content buffer is not guaranteed to be the
        // final cr allocation on every STL).
        REQUIRE(cr.outstanding() == 2 + kContainerOverhead);
        REQUIRE(cr.last_bytes() >= sv.size()); // host allocates sv.size(); >= is the portable pin
    } // ~j -> header and buffer deallocate back into cr
    REQUIRE(cr.outstanding() == 0);            // return proof (destroy() through res)

    // Empty sv edge (same case): SSO => no content buffer, but the
    // pmr::string object header still goes through res; MSVC debug also
    // allocates the per-container proxy, released when e dies.
    {
        Json e(std::string_view{}, &cr);
        REQUIRE(e.is_string());
        REQUIRE(e.as_string().empty());
        REQUIRE(cr.outstanding() == 1 + kContainerOverhead);
    }
    REQUIRE(cr.outstanding() == 0);
}

TEST_CASE("String: own allocates header through resource") {
    TestCountingResource cr;
    std::string src(48, 's'); // > SSO => content buffer must go through res
    {
        String s{std::string_view(src)};
        REQUIRE(!s.is_owned());
        REQUIRE(cr.outstanding() == 0); // borrowed: no allocation yet
        s.own(&cr);
        REQUIRE(s.is_owned());
        // 1 pmr::string object header + 1 content buffer (+ MSVC proxy).
        REQUIRE(cr.outstanding() == 2 + kContainerOverhead);
        REQUIRE(static_cast<std::string_view>(s) == std::string_view(src));
    }
    REQUIRE(cr.outstanding() == 0);

    // Empty edge: SSO content, but the header still goes through cr.
    {
        String e{std::string_view{}};
        e.own(&cr);
        REQUIRE(e.is_owned());
        REQUIRE(cr.outstanding() == 1 + kContainerOverhead);
    }
    REQUIRE(cr.outstanding() == 0);
}

TEST_CASE("String: release hands off ownership") {
    TestCountingResource cr;
    std::string src(48, 'r'); // > SSO => header + buffer
    {
        String s{std::string_view(src)};
        s.own(&cr);
        REQUIRE(cr.outstanding() == 2 + kContainerOverhead);

        std::pmr::string *p = s.release();
        REQUIRE(p != nullptr);
        REQUIRE(!s.is_owned());
        // Handoff does not free: header + buffer still outstanding.
        REQUIRE(cr.outstanding() == 2 + kContainerOverhead);
        REQUIRE(*p == src);

        String::destroy_owned(p); // required release pair (never `delete`)
        REQUIRE(cr.outstanding() == 0);
    }

    // Round-trip: release -> adopt via String(pmr::string*) -> scope exit
    // destroys through the same resource (asserts the new adoption contract).
    {
        String s{std::string_view(src)};
        s.own(&cr);
        std::pmr::string *p = s.release();
        {
            String adopted(p);
            REQUIRE(adopted.is_owned());
            REQUIRE(static_cast<std::string_view>(adopted) == std::string_view(src));
            REQUIRE(cr.outstanding() == 2 + kContainerOverhead);
        } // ~adopted -> destroy_owned(p)
        REQUIRE(cr.outstanding() == 0);
    }
    REQUIRE(cr.outstanding() == 0);
}

TEST_CASE("String: moved-from is a null view") {
    const std::string src(48, 'm'); // > SSO
    // (a) move-ctor <- View: source is zeroed, target content unchanged
    {
        String v{std::string_view(src)};
        String w(std::move(v));
        REQUIRE(!w.is_owned());
        REQUIRE(static_cast<std::string_view>(v).empty());
        REQUIRE(static_cast<std::string_view>(v).data() == nullptr);
        REQUIRE(static_cast<std::string_view>(w) == std::string_view(src));
    }
    // (b) move-ctor <- Owned: source is zeroed, target still owned
    {
        String o{std::string_view(src)};
        o.own(std::pmr::new_delete_resource());
        String w(std::move(o));
        REQUIRE(w.is_owned());
        REQUIRE(static_cast<std::string_view>(o).empty());
        REQUIRE(static_cast<std::string_view>(o).data() == nullptr);
        REQUIRE(static_cast<std::string_view>(w) == std::string_view(src));
    }
    // (c) move-assign <- Owned into empty: placement-new rebuild
    {
        String o{std::string_view(src)};
        o.own(std::pmr::new_delete_resource());
        String d;
        d = std::move(o);
        REQUIRE(d.is_owned());
        REQUIRE(static_cast<std::string_view>(o).empty());
        REQUIRE(static_cast<std::string_view>(o).data() == nullptr);
        REQUIRE(static_cast<std::string_view>(d) == std::string_view(src));
    }
    // (d) move-assign <- Owned into Owned: old target header must be freed
    {
        TestCountingResource cr;
        {
            String a{std::string_view(src)};
            a.own(&cr);
            String b{std::string_view(src)};
            b.own(&cr);
            // Two owned strings: header + buffer each (+ MSVC proxy per container).
            REQUIRE(cr.outstanding() == 4 + 2 * kContainerOverhead);
            b = std::move(a);
            REQUIRE(static_cast<std::string_view>(b) == std::string_view(src));
            REQUIRE(static_cast<std::string_view>(a).empty());
            REQUIRE(static_cast<std::string_view>(a).data() == nullptr);
            // Old b reclaimed; only b's new content remains.
            REQUIRE(cr.outstanding() == 2 + kContainerOverhead);
        }
        REQUIRE(cr.outstanding() == 0);
    }
}

TEST_CASE("String: release leaves a null view") {
    TestCountingResource cr;
    const std::string src(48, 'r');
    String s{std::string_view(src)};
    s.own(&cr);
    std::pmr::string *p = s.release();
    REQUIRE(p != nullptr);
    REQUIRE(!s.is_owned());
    REQUIRE(static_cast<std::string_view>(s).empty());            // pre-fix: false
    REQUIRE(static_cast<std::string_view>(s).data() == nullptr); // pre-fix: non-null
    String::destroy_owned(p);
    REQUIRE(cr.outstanding() == 0);
    // Pre-fix: s still held {freed header, 48}, so own() copied from freed
    // memory (ASan UAF) and .empty() was false.
    s.own(&cr);
    REQUIRE(s.is_owned());
    REQUIRE(static_cast<std::string_view>(s).empty());
    // Borrowed release() stays a no-op: returns nullptr, content preserved.
    String v{std::string_view(src)};
    REQUIRE(v.release() == nullptr);
    REQUIRE(static_cast<std::string_view>(v) == std::string_view(src));
}

TEST_CASE("Json: clone allocates header through resource") {
    TestCountingResource cr;
    static const char kCloneSrc[] = "clone-source-long-enough-to-exceed-sso-capacity";
    {
        Json src(kCloneSrc); // borrowed view, no allocation
        Json c = src.clone(&cr);
        REQUIRE(c.is_string());
        REQUIRE(c.as_string() == kCloneSrc);
        // 1 cloned pmr::string header + 1 content buffer (+ MSVC proxy).
        REQUIRE(cr.outstanding() == 2 + kContainerOverhead);
    }
    REQUIRE(cr.outstanding() == 0);
}

TEST_CASE("Json: clone propagates string allocation failure") {
    // Resource that fails a chosen allocation, so the clone String arm's
    // make_owned throws mid-construction. Regression pin for the
    // "allocate first, tag second" order (R_51 F1): pre-fix, unwinding ran
    // ~Json -> destroy_owned on a never-assigned heap pointer (null deref /
    // UB); post-fix the bad_alloc propagates and make_owned's catch(...)
    // returns the raw header block to the resource.
    struct ThrowOnAllocResource : std::pmr::memory_resource
    {
        std::pmr::memory_resource *up = std::pmr::new_delete_resource();
        int allocs = 0;
        int fail_at = 0; // 1-based allocation to fail; 0 = never
        long long outstanding = 0;

    protected:
        void *do_allocate(std::size_t n, std::size_t align) override
        {
            ++allocs;
            if (allocs == fail_at)
                throw std::bad_alloc();
            ++outstanding;
            return up->allocate(n, align);
        }
        void do_deallocate(void *p, std::size_t n, std::size_t align) override
        {
            --outstanding;
            up->deallocate(p, n, align);
        }
        bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override
        {
            return this == &other;
        }
    };

    ThrowOnAllocResource res;
    res.fail_at = 2; // #1 header, #2 content buffer (MSVC proxy may interleave)
    Json src("clone-source-long-enough-to-exceed-sso-capacity");
    REQUIRE_THROWS_AS((void)src.clone(&res), std::bad_alloc);
    REQUIRE(res.outstanding == 0); // header deallocated on throw
}

TEST_CASE("Json: owned string outlives source") {
    std::pmr::memory_resource *mr = std::pmr::new_delete_resource();
    Json j;
    {
        std::string tmp(48, 'a');
        j = Json(std::string_view(tmp), mr);
    } // tmp dies here; j's buffer lives on in mr
    REQUIRE(j.is_string());
    REQUIRE(j.as_string() == std::string(48, 'a'));
}

TEST_CASE("Json: own factory default resource") {
    Json j;
    {
        std::string tmp(48, 'b');
        j = Json::own(std::string_view(tmp)); // default res = Config global
        REQUIRE(j.as_string().data() != tmp.data()); // anti-borrow pin: owned buffer never aliases the source
    }
    REQUIRE(j.is_string());
    REQUIRE(j.as_string() == std::string(48, 'b')); // survival + content

    // const char* spelling (literal -> string_view UDC)
    static const char kLit[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"; // 48 b's
    Json jl = Json::own(kLit);
    REQUIRE(jl.is_string());
    REQUIRE(jl.as_string() == std::string_view(kLit, 48));
}

TEST_CASE("Json: literal ctor stays borrowed") {
    // Anti-footgun regression pin: the 1-arg spelling must still borrow
    // (C8 identity). If the owned ctor ever steals literals, data() would
    // become a heap address (red); if it gains a defaulted res parameter,
    // Json("lit") becomes an ambiguous compile error (red).
    static const char kLit[] = "cccccccccccccccccccccccccccccccccccccccccccccccc"; // 48 c's
    Json j(kLit); // 1-arg -> C8 identity (const char*)
    REQUIRE(j.is_string());
    REQUIRE(j.as_string().data() == kLit); // pointer identity = zero-copy proof
    REQUIRE(j.as_string() == std::string_view(kLit, 48));
}

TEST_CASE("Json: owned ctor from std::string") {
    std::pmr::memory_resource *mr = std::pmr::new_delete_resource();
    // lvalue spelling: string -> string_view UDC -> owned ctor (no
    // by-value string ctor may exist — the house rule)
    std::string src(48, 'd');
    Json j(src, mr);
    REQUIRE(j.is_string());
    REQUIRE(j.as_string() == std::string(48, 'd'));

    // rvalue spelling
    Json j2(std::move(std::string(48, 'd')), mr);
    REQUIRE(j2.is_string());
    REQUIRE(j2.as_string() == std::string(48, 'd'));
}

TEST_CASE("Json: owned ctor overload set (compile pins)") {
    // The ambiguity matrix's compile-time wall (plan 18 §2.2): the 2-arg
    // owned ctor is the only 2-arg candidate for string-ish first args, and
    // no non-string first arg may reach it. constructible_from (C++20) is
    // the construct-side trait (is_invocable tests calls, not ctors).
    static_assert(std::constructible_from<Json, std::string_view, std::pmr::memory_resource *>);
    static_assert(std::constructible_from<Json, const char *, std::pmr::memory_resource *>);
    static_assert(std::constructible_from<Json, std::string, std::pmr::memory_resource *>);
    static_assert(!std::constructible_from<Json, const Json &, std::pmr::memory_resource *>);
    static_assert(!std::constructible_from<Json, bool, std::pmr::memory_resource *>);
}

// --- task 19: get<T> / try_get<T> / is_integer ---

// 8-slot fixture: every Json tag exactly once. StringOwned slot is produced
// through the existing clone path (StringView clone = StringOwned,
// src/json.cpp:83-93) — no task-18 dependency.
// slot 0 = Null, 1 = Boolean, 2 = Integer, 3 = Floating, 4 = StringView,
//      5 = StringOwned, 6 = Array, 7 = Object
static Array make_slots()
{
    Json owned_str = parse_copy("\"s\"").root().clone();
    return Array::of(Json(nullptr), Json(true), Json((int64_t)7),
                      Json(1.5), Json("s"), std::move(owned_str),
                      Json(Array{}), Json(Object{}));
}

TEST_CASE("Json: is_integer type test") {
    Array slots = make_slots();
    REQUIRE(slots.size() == 8);
    // Alias identity on all 8 slots
    for (size_t i = 0; i < slots.size(); ++i)
        REQUIRE(slots[i].is_integer() == slots[i].is_int());
    // Per-slot values (type-based, not value-based)
    REQUIRE(!slots[0].is_integer());
    REQUIRE(!slots[1].is_integer());
    REQUIRE(slots[2].is_integer());
    REQUIRE(!slots[3].is_integer());
    REQUIRE(!slots[4].is_integer());
    REQUIRE(!slots[5].is_integer());
    REQUIRE(!slots[6].is_integer());
    REQUIRE(!slots[7].is_integer());

    // constexpr pin (literal_test.cpp precedent): the alias is constexpr-capable
    constexpr Json c((int64_t)7);
    static_assert(c.is_integer());
    static_assert(!c.is_float());
}

TEST_CASE("Json: get numeric matrix") {
    Array slots = make_slots();
    // Full 32-cell matrix of plan 19 §2.2, same shape in debug and release
    // (no #ifdef): rejected cells throw TypeError, accepted cells return.

    // Accepted cells (value pins)
    REQUIRE(slots[1].get<bool>() == true);
    REQUIRE(slots[2].get<int64_t>() == (int64_t)7);
    REQUIRE(slots[2].get<float>() == 7.0f);  // int64 -> float (defined rounding, exact at 7)
    REQUIRE(slots[2].get<double>() == 7.0);  // int64 -> double (defined widening)
    REQUIRE(slots[3].get<float>() == 1.5f);  // double -> float (exact)
    REQUIRE(slots[3].get<double>() == 1.5);  // identity

    // T = bool: Boolean only
    REQUIRE_THROWS_AS((void)slots[0].get<bool>(), TypeError);
    REQUIRE_THROWS_AS((void)slots[2].get<bool>(), TypeError);
    REQUIRE_THROWS_AS((void)slots[3].get<bool>(), TypeError);
    REQUIRE_THROWS_AS((void)slots[4].get<bool>(), TypeError);
    REQUIRE_THROWS_AS((void)slots[5].get<bool>(), TypeError);
    REQUIRE_THROWS_AS((void)slots[6].get<bool>(), TypeError);
    REQUIRE_THROWS_AS((void)slots[7].get<bool>(), TypeError);

    // T = int64_t: Integer only (strict — a Floating source is rejected)
    REQUIRE_THROWS_AS((void)slots[0].get<int64_t>(), TypeError);
    REQUIRE_THROWS_AS((void)slots[1].get<int64_t>(), TypeError);
    REQUIRE_THROWS_AS((void)slots[3].get<int64_t>(), TypeError);
    REQUIRE_THROWS_AS((void)slots[4].get<int64_t>(), TypeError);
    REQUIRE_THROWS_AS((void)slots[5].get<int64_t>(), TypeError);
    REQUIRE_THROWS_AS((void)slots[6].get<int64_t>(), TypeError);
    REQUIRE_THROWS_AS((void)slots[7].get<int64_t>(), TypeError);

    // T = float: Integer + Floating
    REQUIRE_THROWS_AS((void)slots[0].get<float>(), TypeError);
    REQUIRE_THROWS_AS((void)slots[1].get<float>(), TypeError);
    REQUIRE_THROWS_AS((void)slots[4].get<float>(), TypeError);
    REQUIRE_THROWS_AS((void)slots[5].get<float>(), TypeError);
    REQUIRE_THROWS_AS((void)slots[6].get<float>(), TypeError);
    REQUIRE_THROWS_AS((void)slots[7].get<float>(), TypeError);

    // T = double: Integer + Floating
    REQUIRE_THROWS_AS((void)slots[0].get<double>(), TypeError);
    REQUIRE_THROWS_AS((void)slots[1].get<double>(), TypeError);
    REQUIRE_THROWS_AS((void)slots[4].get<double>(), TypeError);
    REQUIRE_THROWS_AS((void)slots[5].get<double>(), TypeError);
    REQUIRE_THROWS_AS((void)slots[6].get<double>(), TypeError);
    REQUIRE_THROWS_AS((void)slots[7].get<double>(), TypeError);
}

TEST_CASE("Json: try_get numeric matrix") {
    Array slots = make_slots();
    // Same 32 cells, nullopt/has_value form (noexcept track)
    // Accepted cells (value pins)
    REQUIRE(slots[1].try_get<bool>().has_value());
    REQUIRE(*slots[1].try_get<bool>() == true);
    REQUIRE(slots[2].try_get<int64_t>().has_value());
    REQUIRE(*slots[2].try_get<int64_t>() == (int64_t)7);
    REQUIRE(*slots[2].try_get<float>() == 7.0f);
    REQUIRE(*slots[2].try_get<double>() == 7.0);
    REQUIRE(*slots[3].try_get<float>() == 1.5f);
    REQUIRE(*slots[3].try_get<double>() == 1.5);

    // T = bool: Boolean only
    REQUIRE(!slots[0].try_get<bool>().has_value());
    REQUIRE(!slots[2].try_get<bool>().has_value());
    REQUIRE(!slots[3].try_get<bool>().has_value());
    REQUIRE(!slots[4].try_get<bool>().has_value());
    REQUIRE(!slots[5].try_get<bool>().has_value());
    REQUIRE(!slots[6].try_get<bool>().has_value());
    REQUIRE(!slots[7].try_get<bool>().has_value());

    // T = int64_t: Integer only (strict)
    REQUIRE(!slots[0].try_get<int64_t>().has_value());
    REQUIRE(!slots[1].try_get<int64_t>().has_value());
    REQUIRE(!slots[3].try_get<int64_t>().has_value());
    REQUIRE(!slots[4].try_get<int64_t>().has_value());
    REQUIRE(!slots[5].try_get<int64_t>().has_value());
    REQUIRE(!slots[6].try_get<int64_t>().has_value());
    REQUIRE(!slots[7].try_get<int64_t>().has_value());

    // T = float: Integer + Floating
    REQUIRE(!slots[0].try_get<float>().has_value());
    REQUIRE(!slots[1].try_get<float>().has_value());
    REQUIRE(!slots[4].try_get<float>().has_value());
    REQUIRE(!slots[5].try_get<float>().has_value());
    REQUIRE(!slots[6].try_get<float>().has_value());
    REQUIRE(!slots[7].try_get<float>().has_value());

    // T = double: Integer + Floating
    REQUIRE(!slots[0].try_get<double>().has_value());
    REQUIRE(!slots[1].try_get<double>().has_value());
    REQUIRE(!slots[4].try_get<double>().has_value());
    REQUIRE(!slots[5].try_get<double>().has_value());
    REQUIRE(!slots[6].try_get<double>().has_value());
    REQUIRE(!slots[7].try_get<double>().has_value());

    // Consistency law over all 8 slots: is_* true iff the matching try_get
    // has a value (delivery-surface self-consistency pin)
    for (const auto &j : slots)
    {
        REQUIRE(j.is_integer() == j.try_get<int64_t>().has_value());
        REQUIRE(j.is_number() == j.try_get<double>().has_value());
        REQUIRE(j.is_boolean() == j.try_get<bool>().has_value());
    }
}

TEST_CASE("Json: get widening boundary") {
    // int64 -> double: the five pins of plan 19 §2.5 (round to even)
    REQUIRE(Json((int64_t)9007199254740991).get<double>() == 9007199254740991.0); // 2^53-1, exact
    REQUIRE(Json((int64_t)9007199254740992).get<double>() == 9007199254740992.0); // 2^53, exact
    // 2^53+1 is the exact midpoint of 2^53 and 2^53+2 => tie => the even
    // mantissa is 2^53 (rounds DOWN, NOT to 2^53+2 = 9007199254740994.0)
    REQUIRE(Json((int64_t)9007199254740993).get<double>() == 9007199254740992.0);
    REQUIRE(Json(std::numeric_limits<int64_t>::max()).get<double>() == 9223372036854775808.0);   // 2^63
    REQUIRE(Json(std::numeric_limits<int64_t>::min()).get<double>() == -9223372036854775808.0);  // -2^63

    // int64 -> float: 2^24+1 is a tie -> even mantissa 2^24; 2^53 exact
    // (a power of two); Floating 1.5 -> 1.5f exact (identity, not conversion)
    REQUIRE(Json((int64_t)16777217).get<float>() == 16777216.0f);
    REQUIRE(Json((int64_t)9007199254740992).get<float>() == 9007199254740992.0f);
    Json f(1.5);
    REQUIRE(f.get<float>() == 1.5f);
}

TEST_CASE("Json: get strict integer direction") {
    Json v(5.0); // integer-valued, but stored in the Floating slot
    REQUIRE_THROWS_AS((void)v.get<int64_t>(), TypeError);
    REQUIRE(!v.try_get<int64_t>().has_value());
    REQUIRE(v.is_float());
    REQUIRE(!v.is_integer());

    // Parse side: 5.0 still lands in the Floating slot (task-06 invariant)
    auto doc = parse_copy("5.0");
    const Json &r = doc.root();
    REQUIRE_THROWS_AS((void)r.get<int64_t>(), TypeError);
    REQUIRE(!r.try_get<int64_t>().has_value());
    REQUIRE(r.is_float());
    REQUIRE(!r.is_integer());
    REQUIRE(r.try_get<double>().has_value()); // numeric-domain read still works
    REQUIRE(*r.try_get<double>() == 5.0);
}

// Negative compile pins need the detection-idiom form: naming an
// unsatisfied member-template specialization directly (&Json::get<int>)
// is a hard error on both clang 22 and gcc 16, while the requires-clause
// check on a call expression is SFINAE (immediate context) — verified
// empirically on both host compilers before use.
template <typename T, typename = void>
struct can_get : std::false_type {};
template <typename T>
struct can_get<T, std::void_t<decltype(std::declval<const Json &>().get<T>())>>
    : std::true_type {};

template <typename T, typename = void>
struct can_try_get : std::false_type {};
template <typename T>
struct can_try_get<T, std::void_t<decltype(std::declval<const Json &>().try_get<T>())>>
    : std::true_type {};

TEST_CASE("Json: get compile pins") {
    // Positive pins: the four legal T's are invocable (requires clause)
    static_assert(std::is_invocable_v<decltype(&Json::get<bool>), const Json &>);
    static_assert(std::is_invocable_v<decltype(&Json::get<int64_t>), const Json &>);
    static_assert(std::is_invocable_v<decltype(&Json::get<float>), const Json &>);
    static_assert(std::is_invocable_v<decltype(&Json::get<double>), const Json &>);
    static_assert(std::is_invocable_v<decltype(&Json::try_get<bool>), const Json &>);
    // Negative pins: T outside the set is a compile error (deduction failure)
    static_assert(!can_get<int>::value);           // 32-bit narrowing rejected
    static_assert(!can_get<uint64_t>::value);       // no unsigned slot
    static_assert(!can_try_get<std::string>::value);
}

// --- task 21: Json range-for + Object::keys()/values() ---

TEST_CASE("Json: range-for over array") {
    Json two = Json(Array::of(Json((int64_t)1), Json((int64_t)2), Json((int64_t)3)));
    size_t count = 0;
    int64_t sum = 0;
    for (auto &&e : two)
    {
        REQUIRE(e.key.empty());  // array element: the key side is always the empty view
        sum += e.value.as_int();
        ++count;
    }
    REQUIRE(count == 3);
    REQUIRE(sum == 6);

    // In-loop patch: e.value is a true reference (non-const value side)
    for (auto &&e : two)
        e.value = Json((int64_t)99);
    REQUIRE(two[0] == (int64_t)99);

    // plan 21 §3.1c @code example (folded in verbatim — the doxygen
    // example cannot rot)
    auto doc = parse_copy(R"({"nums":[1,2,3]})");
    for (auto &&e : doc.root()["nums"])
        e.value = Json(e.value.as_int() * 2);
    REQUIRE(doc.root()["nums"][0] == (int64_t)2);
    REQUIRE(doc.root()["nums"][1] == (int64_t)4);
    REQUIRE(doc.root()["nums"][2] == (int64_t)6);
}

TEST_CASE("Json: range-for over object entries") {
    Object o;
    o.insert("a", Json((int64_t)1));
    o.insert("b", Json((int64_t)2));
    o.insert("c", Json((int64_t)3));
    Json j(std::move(o));

    std::vector<std::string> keys;
    for (auto &&e : j)
    {
        keys.emplace_back(e.key);      // key: read-only consumption
        e.value = Json((int64_t)42);   // in-loop value patch
    }
    REQUIRE(keys.size() == 3);
    REQUIRE(keys[0] == "a");  // entry order = vector (insertion) order
    REQUIRE(keys[1] == "b");
    REQUIRE(keys[2] == "c");
    REQUIRE(j.at("a") == (int64_t)42);  // patch visible after the loop
    REQUIRE(j.at("b") == (int64_t)42);
    REQUIRE(j.at("c") == (int64_t)42);
}

TEST_CASE("Json: entry view const track (compile pins)") {
    // Iterator split: non-const Json vs const Json (the R2 wall)
    static_assert(std::is_same_v<decltype(std::declval<Json &>().begin()), JsonIterator>);
    static_assert(std::is_same_v<decltype(std::declval<const Json &>().begin()), ConstJsonIterator>);
    // Value side: patchable on the non-const track, sealed on the const track
    static_assert(std::is_same_v<decltype(std::declval<EntryView &>().value), Json &>);
    static_assert(std::is_same_v<decltype(std::declval<ConstEntryView &>().value), const Json &>);
    // Key side: read-only view on both tracks
    static_assert(std::is_same_v<decltype(std::declval<EntryView &>().key), std::string_view>);
    static_assert(std::is_same_v<decltype(std::declval<ConstEntryView &>().key), std::string_view>);
    // Views are single-pointer projections (zero allocation)
    static_assert(sizeof(KeysView) == sizeof(const void *));
    static_assert(sizeof(ValuesView) == sizeof(const void *));
    static_assert(sizeof(ConstValuesView) == sizeof(const void *));
    // Layout zero change: Json stays 24 bytes
    static_assert(sizeof(Json) == 24);

    // Runtime: the const track is usable (compiles AND runs on const Json)
    auto doc = parse_copy(R"({"a":1,"b":2})");
    const Json &cj = doc.root();
    size_t n = 0;
    int64_t sum = 0;
    for (const auto &e : cj)
    {
        sum += e.value.as_int();
        ++n;
    }
    REQUIRE(n == 2);
    REQUIRE(sum == 3);
}

TEST_CASE("Json: begin() on scalar throws TypeError") {
    Json scalars[5] = {Json(nullptr), Json(true), Json((int64_t)1),
                       Json(1.5), Json("s")};
    for (size_t i = 0; i < 5; ++i)
    {
        Json &j = scalars[i];
        // Non-const track (both build modes throw — the at() family)
        REQUIRE_THROWS_AS((void)j.begin(), TypeError);
        REQUIRE_THROWS_AS((void)j.end(), TypeError);
        REQUIRE_THROWS_AS((void)j.keys(), TypeError);
        REQUIRE_THROWS_AS((void)j.values(), TypeError);
        // Const track: the same throw projection
        const Json &cj = j;
        REQUIRE_THROWS_AS((void)cj.begin(), TypeError);
        REQUIRE_THROWS_AS((void)cj.end(), TypeError);
        REQUIRE_THROWS_AS((void)cj.keys(), TypeError);
        REQUIRE_THROWS_AS((void)cj.values(), TypeError);
    }
    // Container but not an object: keys()/values() throw on an array too
    Json arr = Json(Array::of(Json((int64_t)1)));
    REQUIRE_THROWS_AS((void)arr.keys(), TypeError);
    REQUIRE_THROWS_AS((void)arr.values(), TypeError);
    const Json &carr = arr;
    REQUIRE_THROWS_AS((void)carr.keys(), TypeError);
    REQUIRE_THROWS_AS((void)carr.values(), TypeError);
}

TEST_CASE("Json: empty container range-for") {
    Json arr = Json(Array{});
    REQUIRE(arr.begin() == arr.end());
    size_t n = 0;
    for (const auto &e : arr)
    {
        (void)e;
        ++n;
    }
    REQUIRE(n == 0);

    Json obj = Json(Object{});
    REQUIRE(obj.begin() == obj.end());
    n = 0;
    for (const auto &e : obj)
    {
        (void)e;
        ++n;
    }
    REQUIRE(n == 0);
}

TEST_CASE("Object: keys() view order and shape") {
    Object o;
    o.insert("b", Json((int64_t)1));
    o.insert("a", Json((int64_t)2));
    o.insert("c", Json((int64_t)3));
    std::vector<std::string> got;
    for (std::string_view k : o.keys())
        got.emplace_back(k);
    REQUIRE(got.size() == 3);
    REQUIRE(got[0] == "b");  // entry order = insertion order
    REQUIRE(got[1] == "a");
    REQUIRE(got[2] == "c");

    // Duplicate-key overwrite keeps the first key position (task 10 last-wins)
    Object d;
    d.insert("a", Json((int64_t)1));
    d.insert("a", Json((int64_t)2));
    d.insert("b", Json((int64_t)3));
    REQUIRE(d.size() == 2);
    REQUIRE(d.at("a") == (int64_t)2);  // last value wins
    std::vector<std::string> dk;
    for (std::string_view k : d.keys())
        dk.emplace_back(k);
    REQUIRE(dk.size() == 2);
    REQUIRE(dk[0] == "a");
    REQUIRE(dk[1] == "b");

    // Const track: callable and runnable on a const Object
    const Object &co = o;
    size_t n = 0;
    for (std::string_view k : co.keys())
    {
        (void)k;
        ++n;
    }
    REQUIRE(n == 3);
}

TEST_CASE("Object: values() view") {
    // Yield types: Json & (mutable track) / const Json & (const track)
    static_assert(std::is_same_v<decltype(*std::declval<ValuesView::ValueIt>()), Json &>);
    static_assert(std::is_same_v<decltype(*std::declval<ConstValuesView::ValueIt>()), const Json &>);

    Object o;
    o.insert("x", Json((int64_t)1));
    o.insert("y", Json((int64_t)2));
    for (auto &v : o.values())
        v = Json((int64_t)99);  // reference yield: patch in place
    REQUIRE(o.at("x") == (int64_t)99);
    REQUIRE(o.at("y") == (int64_t)99);

    // Const track: read-only yield
    const Object &co = o;
    int seen = 0;
    for (const auto &v : co.values())
    {
        REQUIRE(v == (int64_t)99);
        ++seen;
    }
    REQUIRE(seen == 2);
}

TEST_CASE("Json: keys()/values() dispatch") {
    auto doc = parse_copy(R"({"a":1,"b":2,"c":3})");
    Json &root = doc.root();

    size_t k = 0;
    for (std::string_view kv : root.keys())
    {
        (void)kv;
        ++k;
    }
    REQUIRE(k == 3);
    size_t v = 0;
    for (auto &val : root.values())
    {
        REQUIRE(val.is_int());
        ++v;
    }
    REQUIRE(v == 3);

    // Const root: the same dispatch, const projection (the §2.5 sugar is
    // reachable at the parse entry; scalar/array negative pins live in
    // "Json: begin() on scalar throws TypeError")
    const Json &croot = doc.root();
    k = 0;
    for (std::string_view kv : croot.keys())
    {
        (void)kv;
        ++k;
    }
    REQUIRE(k == 3);
    v = 0;
    for (const auto &val : croot.values())
    {
        REQUIRE(val.is_int());
        ++v;
    }
    REQUIRE(v == 3);
}

// --- task 20: visit / as_variant (8-way tag dispatch, v2 wrapper payload) ---
// Payload: std::variant over std::reference_wrapper<T> alternatives
// (raw references are not valid variant alternatives —
// [variant.requirements]). Extract the referent with .get().

TEST_CASE("Json: visit 8-way dispatch") {
    // 8 slots = the 8 Type tags exactly once (task-19 make_slots: slot
    // 0 = Null, 1 = Boolean, 2 = Integer, 3 = Floating, 4 = StringView,
    // 5 = StringOwned, 6 = Array, 7 = Object).
    Array slots = make_slots();
    // visit reaches exactly the expected alternative per tag (8 case hits
    // = the 8-way coverage pin; the const overload is exercised here).
    // Compare by content, never by literal address: MSVC does not merge
    // identical string literals, so pointer == would be host-dependent.
    const std::string_view expected[8] = {"null", "bool", "int", "double",
                                          "string", "string", "array", "object"};
    for (size_t i = 0; i < slots.size(); ++i)
    {
        const Json &j = slots[i];
        REQUIRE(j.visit([](auto &&v) -> std::string_view
        {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::same_as<T, std::monostate>)
                return "null";
            else if constexpr (std::same_as<T, std::reference_wrapper<const bool>>)
                return "bool";
            else if constexpr (std::same_as<T, std::reference_wrapper<const int64_t>>)
                return "int";
            else if constexpr (std::same_as<T, std::reference_wrapper<const double>>)
                return "double";
            else if constexpr (std::same_as<T, std::string_view>)
                return "string";
            else if constexpr (std::same_as<T, std::reference_wrapper<const Array>>)
                return "array";
            else
                return "object";
        }) == expected[i]);
        // Alternative identity + value (the two string tags share the
        // single std::string_view alternative: 8 tags -> 7 alternatives).
        const auto v = j.as_variant();
        switch (i)
        {
        case 0:
            REQUIRE(std::holds_alternative<std::monostate>(v));
            break;
        case 1:
            REQUIRE(std::holds_alternative<std::reference_wrapper<const bool>>(v));
            REQUIRE(std::get<std::reference_wrapper<const bool>>(v).get());
            break;
        case 2:
            REQUIRE(std::holds_alternative<std::reference_wrapper<const int64_t>>(v));
            REQUIRE(std::get<std::reference_wrapper<const int64_t>>(v).get() == (int64_t)7);
            break;
        case 3:
            REQUIRE(std::holds_alternative<std::reference_wrapper<const double>>(v));
            REQUIRE(std::get<std::reference_wrapper<const double>>(v).get() == 1.5);
            break;
        case 4:
        case 5:
            REQUIRE(std::holds_alternative<std::string_view>(v));
            REQUIRE(std::get<std::string_view>(v) == "s");
            break;
        case 6:
            REQUIRE(std::holds_alternative<std::reference_wrapper<const Array>>(v));
            REQUIRE(std::get<std::reference_wrapper<const Array>>(v).get().empty());
            break;
        case 7:
            REQUIRE(std::holds_alternative<std::reference_wrapper<const Object>>(v));
            REQUIRE(std::get<std::reference_wrapper<const Object>>(v).get().empty());
            break;
        }
    }
}

TEST_CASE("Json: visit null is monostate") {
    Json j(nullptr);
    REQUIRE(std::holds_alternative<std::monostate>(j.as_variant()));
    // The monostate overload is the one the null tag dispatches to.
    REQUIRE(j.visit([](auto &&v) -> int
    {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::same_as<T, std::monostate>)
            return 42;
        else
            return -1;
    }) == 42);
}

TEST_CASE("Json: as_variant payload table") {
    // The 7-alternative spellings, compile-pinned: the 8-tag -> 7-alt
    // collapse is a language constraint (variant alternatives must be
    // pairwise distinct non-array object types — raw references rejected,
    // plan 20 v2 §2.1), not a design choice.
    using ConstAlt = decltype(std::declval<const Json &>().as_variant());
    static_assert(std::is_same_v<ConstAlt,
        std::variant<std::monostate,
                     std::reference_wrapper<const bool>,
                     std::reference_wrapper<const int64_t>,
                     std::reference_wrapper<const double>,
                     std::string_view,
                     std::reference_wrapper<const Array>,
                     std::reference_wrapper<const Object>>>);
    using NonConstAlt = decltype(std::declval<Json &>().as_variant());
    static_assert(std::is_same_v<NonConstAlt,
        std::variant<std::monostate,
                     std::reference_wrapper<bool>,
                     std::reference_wrapper<int64_t>,
                     std::reference_wrapper<double>,
                     std::string_view,
                     std::reference_wrapper<Array>,
                     std::reference_wrapper<Object>>>);
    // Both overloads noexcept (trivial getter precedent).
    static_assert(noexcept(std::declval<const Json &>().as_variant()));
    static_assert(noexcept(std::declval<Json &>().as_variant()));
}

TEST_CASE("Json: visit non-const mutation") {
    // int64 patch: 5 -> 7 through the reference_wrapper<int64_t>
    // alternative (the tag itself can never change — R6 pin).
    Json i((int64_t)5);
    i.visit([](auto &&v)
    {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::same_as<T, std::reference_wrapper<int64_t>>)
            v.get() = (int64_t)7;
    });
    REQUIRE(i.as_int() == (int64_t)7);
    REQUIRE(i.is_int());

    // bool flip through the reference_wrapper<bool> alternative
    Json b(true);
    b.visit([](auto &&v)
    {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::same_as<T, std::reference_wrapper<bool>>)
            v.get() = !v.get();
    });
    REQUIRE(b.as_boolean() == false);
    REQUIRE(b.is_boolean());

    // Array patch: push_back through the reference_wrapper<Array>
    // alternative
    Json a(Array::of(Json((int64_t)1)));
    REQUIRE(a.size() == 1);
    a.visit([](auto &&v)
    {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::same_as<T, std::reference_wrapper<Array>>)
            v.get().push_back(Json((int64_t)2));
    });
    REQUIRE(a.size() == 2);
    REQUIRE(a[1] == (int64_t)2);
    REQUIRE(a.is_array());
}

TEST_CASE("Json: visit overload-set visitor") {
    // The non-generic form of the std::visit protocol: 7-overload struct
    // over the wrapper/value payload types (lambdas cannot carry
    // overloads — the doxygen @note). Non-const Json -> non-const
    // wrappers.
    struct V
    {
        long null = 0, boolean = 0, integer = 0, floating = 0,
             string = 0, array = 0, object = 0;

        void operator()(std::monostate) { ++null; }
        void operator()(std::reference_wrapper<bool>) { ++boolean; }
        void operator()(std::reference_wrapper<int64_t>) { ++integer; }
        void operator()(std::reference_wrapper<double>) { ++floating; }
        void operator()(std::string_view) { ++string; }
        void operator()(std::reference_wrapper<Array>) { ++array; }
        void operator()(std::reference_wrapper<Object>) { ++object; }
    };

    V v;
    Json i((int64_t)7);
    i.visit(v);
    REQUIRE(v.integer == 1);
    REQUIRE(v.null + v.boolean + v.floating + v.string
            + v.array + v.object == 0);

    Json o(Object{});
    o.visit(v);
    REQUIRE(v.object == 1);
    REQUIRE(v.integer == 1); // untouched by the second visit
}

// The json.hpp visit() @code block, verbatim at FILE SCOPE: the doxygen
// example must compile and run (8 values, one per tag) — no rot.
// RULING (2026-09-06 green-gate stop): the original in-TEST_CASE-body
// placement of `render` is a nested function definition — ill-formed C++
// on every host ("function definition is not allowed here"); the
// doxygen @code block itself shows file scope, so file scope (anonymous
// namespace, directly above the TEST_CASE, byte-identical body) is the
// faithful form. Placement fix only: no protocol/payload/doxygen change.
namespace {
std::string render(const Json &j)
{
        return j.visit([](auto &&v) -> std::string
        {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::same_as<T, std::monostate>)
                return "null";
            else if constexpr (std::same_as<T, std::reference_wrapper<const bool>>)
                return v.get() ? "true" : "false";
            else if constexpr (std::same_as<T, std::reference_wrapper<const int64_t>>)
                return std::to_string(v.get());
            else if constexpr (std::same_as<T, std::reference_wrapper<const double>>)
                return std::to_string(v.get());
            else if constexpr (std::same_as<T, std::string_view>)
                return std::string(v);
            else if constexpr (std::same_as<T, std::reference_wrapper<const Array>>)
                return "[" + std::to_string(v.get().size()) + "]";
            else
                return "{" + std::to_string(v.get().size()) + "}";
        });
    }
} // namespace

TEST_CASE("Json: visit doxygen example runs") {
    REQUIRE(render(Json(nullptr)) == "null");
    REQUIRE(render(Json(true)) == "true");
    REQUIRE(render(Json((int64_t)7)) == "7");
    REQUIRE(render(Json(1.5)) == std::to_string(1.5));
    REQUIRE(render(Json("hello")) == "hello");
    REQUIRE(render(Json::own("world")) == "world");
    REQUIRE(render(Json(Array::of(Json((int64_t)1), Json((int64_t)2)))) == "[2]");
    // RULING (2026-09-06 third stop, runtime red): the @code example's
    // Object branch is SIZE-BASED ("{" + to_string(size) + "}") — an
    // empty Object renders "{0}", not JSON "{}"; the plan family's
    // example and case-6 expectation never co-executed until this gate
    // (plan 20 carried both texts; the example is the sacred object,
    // the pin pins reality — test-side literal only, doxygen untouched).
    REQUIRE(render(Json(Object{})) == "{0}"); // size-based, not JSON
}

TEST_CASE("Json: as_variant lifetime window") {
    // Valid-window pin (plan 20 §5 R1): the variant is used while the
    // Json — and the borrowed string's source buffer — stay alive. The
    // StringView alternative is white-box pinned to point AT the source
    // buffer (data() identity). Dangling is UB: untestable, not pinned.
    std::string buf = "s";
    // RULING (2026-09-06 second green-gate stop): the original
    // `Json j(std::string_view(buf));` is the MOST VEXING PARSE — the
    // grammar reads it as a function declaration (param
    // std::string_view named buf), so j.as_variant()/j.visit() fail and
    // -Wvexing-parse fires; host-independent (clang + g++ repro,
    // /tmp/opencode/task20-mvp/). Brace form = single-element list-init
    // into the SAME single-arg ctor, identical overload resolution,
    // verified 0 errors 0 warnings on both hosts:
    Json j{std::string_view(buf)};
    {
        auto v = j.as_variant(); // variant declared
        REQUIRE(std::get<std::string_view>(v).data() == buf.data());
        REQUIRE(std::get<std::string_view>(v) == "s");
        REQUIRE(j.visit([](auto &&x) -> bool
        {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::same_as<T, std::string_view>)
                return x == "s";
            else
                return false;
        })); // used, Json alive throughout
    }
    REQUIRE(j.is_string()); // Json still alive after the window
}

TEST_CASE("Json: const array range-for iterates") {
    // R_21 F1 regression pin (fixer sub-batch, R_21 fixer scope): the
    // array branch of Json::end() const used to return the BEGIN position
    // (copy-paste slip from the adjacent begin() const body) — range-for
    // over a NON-EMPTY const array then compared begin() == end() at
    // position 0 and silently iterated ZERO times (no crash, no throw —
    // the "silent zero-iteration buries the bug" mode ruling C rejects for
    // scalars). No committed case could fire this: case 3's const loop is
    // an object, case 5's range expressions bind non-const Json —
    // 121/121 x3 green was behaviorally consistent with the defect.
    // const Json lvalue straight from the Array::of prvalue (Json(Array)
    // ctor, elided — Array is non-copyable, array.hpp:53, so no
    // Array lvalue may ever feed the by-value ctor).
    const Json ca = Array::of(Json((int64_t)10), Json((int64_t)20),
                              Json((int64_t)30));
    int n = 0;
    const int64_t expect[3] = {10, 20, 30};
    for (const auto &e : ca)
    {
        REQUIRE(e.key.empty()); // array element => empty key view (convention)
        REQUIRE(e.value.as_int() == expect[n]);
        ++n;
    }
    REQUIRE(n == 3); // pre-fix red: n stays 0 — zero iterations, not a crash
    REQUIRE(ca.begin() != ca.end()); // R2 wall: non-empty => distinct
}

// --- task 22: as_*_strict (type check throws in both build modes) ---

TEST_CASE("Json: strict accessors match passthrough") {
    Array slots = make_slots();
    // Per-family value identity on the match slot (both build modes; the
    // guard passes on a matching tag, so no gate is needed here).
    REQUIRE(slots[0].as_null_strict() == nullptr);
    REQUIRE(slots[1].as_boolean_strict() == true);
    REQUIRE(slots[2].as_int_strict() == (int64_t)7);
    REQUIRE(slots[3].as_float_strict() == 1.5);
    REQUIRE(slots[4].as_string_strict() == "s");
    REQUIRE(slots[5].as_string_strict() == "s"); // StringOwned arm
    REQUIRE(slots[6].as_array_strict().size() == 0);
    REQUIRE(slots[7].as_object_strict().size() == 0);

    // Cross-pin against the as_* fast path (match slots are safe in both
    // modes: the tag confirms the active slot before any read).
    REQUIRE(slots[0].as_null() == slots[0].as_null_strict());
    REQUIRE(slots[1].as_boolean() == slots[1].as_boolean_strict());
    REQUIRE(slots[2].as_int() == slots[2].as_int_strict());
    REQUIRE(slots[3].as_float() == slots[3].as_float_strict());
    REQUIRE(slots[4].as_string() == slots[4].as_string_strict());
    REQUIRE(slots[5].as_string() == slots[5].as_string_strict());
    REQUIRE(slots[6].as_array().size() == slots[6].as_array_strict().size());
    REQUIRE(slots[7].as_object().size() == slots[7].as_object_strict().size());

    // Const-overload callability (compile pin): each const twin resolves on
    // a const slot — the 7 const overloads (the 5 non-const ones are
    // exercised by the value pins above; 12 overloads in total).
    const Json &cj = slots[2];
    REQUIRE(cj.as_int_strict() == (int64_t)7);
    const Json &cnull = slots[0];
    REQUIRE(cnull.as_null_strict() == nullptr);
    const Json &cbool = slots[1];
    REQUIRE(cbool.as_boolean_strict() == true);
    const Json &cfloat = slots[3];
    REQUIRE(cfloat.as_float_strict() == 1.5);
    const Json &cstr1 = slots[4];
    REQUIRE(cstr1.as_string_strict() == "s");
    const Json &cstr2 = slots[5];
    REQUIRE(cstr2.as_string_strict() == "s");
    const Json &carr = slots[6];
    REQUIRE(carr.as_array_strict().size() == 0);
    const Json &cobj = slots[7];
    REQUIRE(cobj.as_object_strict().size() == 0);

    // Non-const reference writability: patch through the strict reference
    // (size +1, tag unchanged).
    size_t before = slots[6].size();
    slots[6].as_array_strict().push_back(Json((int64_t)1));
    REQUIRE(slots[6].size() == before + 1);
    REQUIRE(slots[6].is_array());
}

TEST_CASE("Json: strict accessors throw on mismatch") {
    Array slots = make_slots();

    // Named worst-class pins: every one of these call sites is exactly the
    // release wrong-slot-read UB site of the as_* twin (garbage-pointer
    // deref for array/object, {ptr,len} skew for string, denormal bit
    // pattern for float) — _strict must THROW, not crash.
    REQUIRE_THROWS_AS((void)slots[2].as_array_strict(), TypeError);
    REQUIRE_THROWS_AS((void)slots[3].as_object_strict(), TypeError);
    REQUIRE_THROWS_AS((void)slots[2].as_string_strict(), TypeError);
    REQUIRE_THROWS_AS((void)slots[2].as_float_strict(), TypeError);
    REQUIRE_THROWS_AS((void)slots[4].as_boolean_strict(), TypeError);

    // Consistency law (8 slots x 7 families = 56 pins): is_X false iff
    // as_X_strict throws — the family self-consistency wall.
    for (const Json &j : slots)
    {
        REQUIRE(j.is_null() == !threw_type_error([&] { (void)j.as_null_strict(); }));
        REQUIRE(j.is_boolean() == !threw_type_error([&] { (void)j.as_boolean_strict(); }));
        REQUIRE(j.is_int() == !threw_type_error([&] { (void)j.as_int_strict(); }));
        REQUIRE(j.is_float() == !threw_type_error([&] { (void)j.as_float_strict(); }));
        REQUIRE(j.is_string() == !threw_type_error([&] { (void)j.as_string_strict(); }));
        REQUIRE(j.is_array() == !threw_type_error([&] { (void)j.as_array_strict(); }));
        REQUIRE(j.is_object() == !threw_type_error([&] { (void)j.as_object_strict(); }));
    }
}

TEST_CASE("Json: as_* debug check throws") {
#ifndef NDEBUG
    // Debug-only pins (the gate is a UB-avoidance obligation, not a test
    // split): in debug the as_* fast path checks the tag first
    // (debug_check_type throws TypeError) and only then reads the slot.
    // In release (NDEBUG) these same six calls would read an inactive
    // union member — undefined behavior, the class as_*_strict closes —
    // so they are deliberately NOT executed under NDEBUG (the ungated
    // mismatch pins live in "Json: strict accessors throw on mismatch").
    Array slots = make_slots();
    REQUIRE_THROWS_AS((void)slots[4].as_boolean(), TypeError);
    REQUIRE_THROWS_AS((void)slots[3].as_int(), TypeError);
    REQUIRE_THROWS_AS((void)slots[2].as_float(), TypeError);
    REQUIRE_THROWS_AS((void)slots[2].as_string(), TypeError);
    REQUIRE_THROWS_AS((void)slots[2].as_array(), TypeError);
    REQUIRE_THROWS_AS((void)slots[3].as_object(), TypeError);
#else
    // release: fast path unchecked by contract (as_* @note) — no pins
#endif
}

TEST_CASE("Parser: initial reserve bounded by input") {
    TestCountingResource cr;

    // One array element whose value dwarfs the container: the grammatical
    // bound (remaining / 2) would allow ~100000 entries, i.e. ~2.4 MB of
    // reserve. The 256 KiB byte budget must cap the single reserve
    // allocation. The string is a borrowed view, so the reserve of the
    // entry vector is the only allocation routed through `cr`.
    std::string content = "[\"" + std::string(200000, 'a') + "\"]";
    std::string buf(content.size() + kPaddingWidth, '\0');
    std::memcpy(buf.data(), content.data(), content.size());

    {
        Parser p(std::string_view(buf.data(), content.size()), &cr, true);
        Json root = p.parse();
        REQUIRE(root.as_array().size() == 1);
        REQUIRE(cr.last_bytes() > 0);
        REQUIRE(cr.last_bytes() <= 256 * 1024);
    }
    REQUIRE(cr.outstanding() == 0);
}

TEST_CASE("Parser: initial reserve nested keeps fixed hint") {
    TestCountingResource cr;

    // The nested [1] sees the parent's ~100 KB tail in m_end - m_curr.
    // Without the depth gate it would estimate thousands of entries from
    // those bytes; the fixed hint must keep it at 4.
    std::string content = "[[1],\"" + std::string(100000, 'b') + "\"]";
    std::string buf(content.size() + kPaddingWidth, '\0');
    std::memcpy(buf.data(), content.data(), content.size());

    Parser p(std::string_view(buf.data(), content.size()), &cr, true);
    Json root = p.parse();
    REQUIRE(root.is_array());
    REQUIRE(root.as_array()[0].is_array());
    REQUIRE(root.as_array()[0].as_array().data().capacity() == 4);
}

// --- task 26: std::hash<Json> + operator< + Object::merge ---

TEST_CASE("Json: hash equal values same hash") {
    std::hash<Json> h;
    using E = Object::Entry;

    // Same object content, different insertion order (== is order-insensitive).
    Json o1(Object::of(E{"x", Json((int64_t)1)}, E{"y", Json((int64_t)2)}));
    Json o2(Object::of(E{"y", Json((int64_t)2)}, E{"x", Json((int64_t)1)}));
    REQUIRE(o1 == o2);
    REQUIRE(h(o1) == h(o2));

    // Same string content across the two storage tags (cross-tag ==).
    static const char kLit[] = "hello";
    Json borrowed(kLit);
    Json owned(std::string_view(kLit), std::pmr::new_delete_resource());
    REQUIRE(borrowed == owned);
    REQUIRE(h(borrowed) == h(owned));

    // +0.0 == -0.0 must hash equal (the -0.0 bit-fold).
    Json pos(0.0);
    Json neg(-0.0);
    REQUIRE(pos == neg);
    REQUIRE(h(pos) == h(neg));

    // Deeply equal arrays built two ways.
    Json a1(Array::of(Json((int64_t)1), Json("s"), Json(Object::of(E{"k", Json(true)}))));
    Json a2(Array::of(Json((int64_t)1), Json("s"), Json(Object::of(E{"k", Json(true)}))));
    REQUIRE(a1 == a2);
    REQUIRE(h(a1) == h(a2));
}

TEST_CASE("Json: hash different values distinct (fixed set)") {
    std::hash<Json> h;
    using E = Object::Entry;

    std::vector<Json> vals;
    vals.push_back(Json());
    vals.push_back(Json(true));
    vals.push_back(Json(false));
    vals.push_back(Json((int64_t)0));
    vals.push_back(Json((int64_t)1));
    vals.push_back(Json((int64_t)-1));
    vals.push_back(Json(0.0));
    vals.push_back(Json(1.0));
    vals.push_back(Json(-1.5));
    vals.push_back(Json("a"));
    vals.push_back(Json("b"));
    vals.push_back(Json(Array{}));
    vals.push_back(Json(Array::of(Json((int64_t)1))));
    vals.push_back(Json(Object{}));
    vals.push_back(Json(Object::of(E{"k", Json((int64_t)1)})));
    vals.push_back(Json(Object::of(E{"k", Json((int64_t)2)})));

    for (size_t i = 0; i < vals.size(); ++i)
        for (size_t j = i + 1; j < vals.size(); ++j)
        {
            REQUIRE(vals[i] != vals[j]);
            REQUIRE(h(vals[i]) != h(vals[j]));
        }
}

TEST_CASE("Json: hash NaN callable and stable") {
    std::hash<Json> h;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    Json j(nan);
    REQUIRE(j.is_float());
    const size_t first = h(j);
    const size_t second = h(j);
    REQUIRE(first == second); // same object: stable
    // NaN has no equality obligations; only callable + stable are pinned.
}

TEST_CASE("Json: operator< 8-kind rank order") {
    std::vector<Json> vals;
    vals.push_back(Json());           // rank 0: null
    vals.push_back(Json(false));      // rank 1: boolean
    vals.push_back(Json((int64_t)5)); // rank 2: integer
    vals.push_back(Json(5.0));        // rank 3: floating
    vals.push_back(Json("s"));        // rank 4: string
    vals.push_back(Json(Array{}));    // rank 5: array
    vals.push_back(Json(Object{}));   // rank 6: object

    for (size_t i = 0; i < vals.size(); ++i)
        for (size_t j = i + 1; j < vals.size(); ++j)
        {
            REQUIRE(vals[i] < vals[j]);
            REQUIRE(!(vals[j] < vals[i]));
        }

    // Rank beats value magnitude: int 5 < float 5.0; int -1 < float -1.5.
    REQUIRE(Json((int64_t)5) < Json(5.0));
    REQUIRE(Json((int64_t)-1) < Json(-1.5));
    REQUIRE(Json("s") < Json(Array{}));
}

TEST_CASE("Json: operator< ==/< consistency and transitivity") {
    using E = Object::Entry;
    std::vector<Json> vals;
    vals.push_back(Json());
    vals.push_back(Json(false));
    vals.push_back(Json(true));
    vals.push_back(Json((int64_t)-7));
    vals.push_back(Json((int64_t)0));
    vals.push_back(Json((int64_t)7));
    vals.push_back(Json(-2.5));
    vals.push_back(Json(0.0));
    vals.push_back(Json(2.5));
    vals.push_back(Json("apple"));
    vals.push_back(Json("banana"));
    vals.push_back(Json("s"));
    {
        static const char kS[] = "s";
        vals.push_back(Json(std::string_view(kS), std::pmr::new_delete_resource()));
    }
    vals.push_back(Json(Array{}));
    vals.push_back(Json(Array::of(Json((int64_t)1))));
    vals.push_back(Json(Object{}));
    vals.push_back(Json(Object::of(E{"k", Json((int64_t)1)})));

    for (size_t i = 0; i < vals.size(); ++i)
        for (size_t j = 0; j < vals.size(); ++j)
        {
            const bool lt = vals[i] < vals[j];
            const bool gt = vals[j] < vals[i];
            const bool eq = vals[i] == vals[j];
            const bool consistent = !(lt && gt);
            const bool total = eq || lt || gt;
            const bool eq_incomparable = !lt && !gt;
            REQUIRE(consistent);
            REQUIRE(total);
            if (eq)
                REQUIRE(eq_incomparable);
        }

    // The cross-tag equal pair is equal and incomparable, never tag-ordered.
    REQUIRE(vals[11] == vals[12]);
    REQUIRE(!(vals[11] < vals[12]));
    REQUIRE(!(vals[12] < vals[11]));

    // ±0.0 are equal and mutually incomparable.
    REQUIRE(Json(0.0) == Json(-0.0));
    REQUIRE(!(Json(0.0) < Json(-0.0)));
    REQUIRE(!(Json(-0.0) < Json(0.0)));

    // Transitivity over a 5-chain: low < mid < high implies low < high.
    std::vector<Json> chain;
    chain.push_back(Json());
    chain.push_back(Json((int64_t)-1));
    chain.push_back(Json(0.0));
    chain.push_back(Json("m"));
    chain.push_back(Json(Array::of(Json((int64_t)1))));
    for (size_t i = 0; i + 2 < chain.size(); ++i)
    {
        REQUIRE(chain[i] < chain[i + 1]);
        REQUIRE(chain[i + 1] < chain[i + 2]);
        REQUIRE(chain[i] < chain[i + 2]);
    }
}

TEST_CASE("Json: operator< object canonical (key-sorted) order") {
    using E = Object::Entry;
    Json a(Object::of(E{"a", Json((int64_t)1)}, E{"b", Json((int64_t)2)}));
    Json b(Object::of(E{"b", Json((int64_t)2)}, E{"a", Json((int64_t)1)}));
    // Same key/value multiset => equal, hence mutually incomparable.
    REQUIRE(a == b);
    REQUIRE(!(a < b));
    REQUIRE(!(b < a));

    // Key order decides regardless of insertion order: {a:9} < {b:0}.
    Json c(Object::of(E{"a", Json((int64_t)9)}));
    Json d(Object::of(E{"b", Json((int64_t)0)}));
    REQUIRE(c < d);
    REQUIRE(!(d < c));

    // Insertion-order discriminator: an order-sensitive compare would see
    // {b:2,a:1} after {a:1,b:2}; canonical must see them equal.
    Json e(Object::of(E{"a", Json((int64_t)1)}, E{"b", Json((int64_t)2)}));
    Json f(Object::of(E{"b", Json((int64_t)2)}, E{"a", Json((int64_t)1)}));
    REQUIRE(!(e < f));
    REQUIRE(!(f < e));

    // Same keys, value decides: {a:1,b:2} < {a:1,b:3}.
    Json g(Object::of(E{"a", Json((int64_t)1)}, E{"b", Json((int64_t)2)}));
    Json hh(Object::of(E{"a", Json((int64_t)1)}, E{"b", Json((int64_t)3)}));
    REQUIRE(g < hh);
    REQUIRE(!(hh < g));

    // Shorter canonical prefix first.
    Json i1(Object::of(E{"a", Json((int64_t)1)}));
    Json i2(Object::of(E{"a", Json((int64_t)1)}, E{"b", Json((int64_t)2)}));
    REQUIRE(i1 < i2);
    REQUIRE(!(i2 < i1));
}

TEST_CASE("Json: operator< NaN canonicalization and -0.0") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    Json jnan(nan);

    // NaN is the maximum element: every non-NaN double sorts before it.
    REQUIRE(Json(-1.0e308) < jnan);
    REQUIRE(Json(1.0e308) < jnan);
    REQUIRE(Json(0.0) < jnan);

    // NaN is self-incomparable (both directions false).
    REQUIRE(!(jnan < jnan));

    // ±0.0 are equal and mutually incomparable.
    REQUIRE(Json(0.0) == Json(-0.0));
    REQUIRE(!(Json(0.0) < Json(-0.0)));
    REQUIRE(!(Json(-0.0) < Json(0.0)));
}

TEST_CASE("Json: int and double cross-kind not equal") {
    std::hash<Json> h;
    // Type-strict equality: an int and a float are never equal, are
    // rank-separated by <, and must hash differently.
    REQUIRE(Json((int64_t)1) != Json(1.0));
    REQUIRE(Json((int64_t)1) < Json(1.0));
    REQUIRE(!(Json(1.0) < Json((int64_t)1)));
    REQUIRE(h(Json((int64_t)1)) != h(Json(1.0)));
}

TEST_CASE("Object: merge deep shape") {
    using E = Object::Entry;
    Object dst = Object::of(
        E{"a", Json(Object::of(E{"x", Json((int64_t)1)}))},
        E{"b", Json((int64_t)1)},
        E{"c", Json((int64_t)1)});
    Object src = Object::of(
        E{"a", Json(Object::of(E{"y", Json((int64_t)2)}))},
        E{"b", Json((int64_t)99)},
        E{"c", Json(Array::of(Json((int64_t)7)))},
        E{"d", Json()});

    dst.merge(src);

    // Nested object merges recursively: x survives, y is added.
    REQUIRE(dst["a"].is_object());
    REQUIRE(dst["a"]["x"] == (int64_t)1);
    REQUIRE(dst["a"]["y"] == (int64_t)2);
    // Scalar replaces wholesale.
    REQUIRE(dst["b"] == (int64_t)99);
    // Array replaces wholesale (not concatenated).
    REQUIRE(dst["c"].is_array());
    REQUIRE(dst["c"].as_array().size() == 1);
    REQUIRE(dst["c"][0] == (int64_t)7);
    // null is a value here: it overwrites.
    REQUIRE(dst.contains("d"));
    REQUIRE(dst["d"].is_null());

    // Source is unchanged.
    REQUIRE(src["a"].as_object().size() == 1);
    REQUIRE(src["a"]["y"] == (int64_t)2);
    REQUIRE(src["b"] == (int64_t)99);
    REQUIRE(src["c"].as_array().size() == 1);
    REQUIRE(!src.contains("x"));
}

TEST_CASE("Object: merge order and position") {
    using E = Object::Entry;
    Object dst = Object::of(
        E{"a", Json((int64_t)1)},
        E{"b", Json((int64_t)2)});
    Object src = Object::of(
        E{"b", Json((int64_t)20)},
        E{"c", Json((int64_t)3)},
        E{"d", Json((int64_t)4)});
    dst.merge(src);

    // Existing keys keep their position; new keys append in other's order.
    std::vector<std::string> got;
    for (std::string_view k : dst.keys())
        got.emplace_back(k);
    REQUIRE(got == std::vector<std::string>{"a", "b", "c", "d"});
    REQUIRE(dst["b"] == (int64_t)20);
}

TEST_CASE("Object: merge independence and ownership") {
    Object dst;
    {
        // The source's key lives on a std::string that dies before the
        // check; merge must have owned its copy.
        std::string key = "transient_key";
        Object src;
        src.insert(key, Json((int64_t)1));
        dst.merge(src);
    }
    REQUIRE(dst.contains("transient_key"));
    REQUIRE(dst["transient_key"] == (int64_t)1);

    // Deep clone independence: mutating the source after the merge must
    // not touch the destination.
    Object src2;
    src2.insert("n", Json(Object::of(Object::Entry{"v", Json((int64_t)5)})));
    Object dst2;
    dst2.merge(src2);
    REQUIRE(dst2["n"]["v"] == (int64_t)5);
    src2["n"]["v"] = Json((int64_t)500);
    REQUIRE(dst2["n"]["v"] == (int64_t)5);
}

TEST_CASE("Object: merge self-aliasing and no-op") {
    using E = Object::Entry;
    Object o = Object::of(E{"a", Json((int64_t)1)}, E{"b", Json((int64_t)2)});
    o.merge(o); // direct self-merge is a defined no-op
    REQUIRE(o.size() == 2);
    REQUIRE(o["a"] == (int64_t)1);
    REQUIRE(o["b"] == (int64_t)2);

    // Empty source is a no-op; empty destination picks up the source.
    Object empty;
    o.merge(empty);
    REQUIRE(o.size() == 2);
    Object dst;
    dst.merge(o);
    REQUIRE(dst.size() == 2);
    REQUIRE(dst == o);
}

TEST_CASE("Object: merge maintains hash index") {
    constexpr int N = 40;
    std::vector<std::string> keys;
    keys.reserve(N);
    for (int i = 0; i < N; ++i)
        keys.push_back("base_" + std::to_string(i));

    Object dst;
    for (int i = 0; i < N; ++i)
        dst.insert(keys[i], Json((int64_t)i));
    // Materialise the lookup index (> kIndexThreshold) and prove it is live.
    for (int i = 0; i < N; ++i)
        REQUIRE(dst.contains(keys[i]));

    Object src;
    src.insert("merged_a", Json((int64_t)1000));
    src.insert("merged_b", Json((int64_t)1001));
    dst.merge(src);

    // New keys must be found through the already-built index — the
    // index_note_append correctness pin (a missing note false-misses, red).
    REQUIRE(dst.contains("merged_a"));
    REQUIRE(dst.contains("merged_b"));
    REQUIRE(dst.at("merged_a") == (int64_t)1000);
    REQUIRE(dst.at("merged_b") == (int64_t)1001);
    REQUIRE(dst["merged_a"] == (int64_t)1000);
    REQUIRE(dst.size() == (size_t)(N + 2));

    // == sees the appended entries too.
    Object expected;
    for (int i = 0; i < N; ++i)
        expected.insert(keys[i], Json((int64_t)i));
    expected.insert("merged_a", Json((int64_t)1000));
    expected.insert("merged_b", Json((int64_t)1001));
    REQUIRE(dst == expected);
}

TEST_CASE("Json: std::string temporary borrow is deleted (compile pins)") {
    // Negative pins: an rvalue std::string / std::pmr::string must not reach
    // the borrowed Json/String entry points.
    static_assert(!std::is_constructible_v<Json, std::string>);
    static_assert(!std::is_constructible_v<Json, std::pmr::string>);
    static_assert(!std::is_assignable_v<Json &, std::string>);
    static_assert(!std::is_assignable_v<Json &, std::pmr::string>);
    static_assert(!std::is_constructible_v<String, std::string>);
    static_assert(!std::is_constructible_v<String, std::pmr::string>);

    // Positive pins: borrowed and owned surfaces stay reachable.
    static_assert(std::is_constructible_v<Json, std::string_view>);
    static_assert(std::is_constructible_v<Json, const char *>);
    static_assert(std::is_constructible_v<Json, const std::string &>);
    static_assert(std::is_constructible_v<Json, std::string &>);
    static_assert(std::is_constructible_v<Json, std::pmr::string &>);
    static_assert(std::is_constructible_v<String, std::string_view>);
    static_assert(std::is_constructible_v<String, const std::string &>);
    static_assert(std::is_constructible_v<Json, std::string_view,
                                          std::pmr::memory_resource *>);

    // Runtime positive: a live lvalue borrow stays zero-copy.
    std::string s(48, 'L');
    Json j(s);
    REQUIRE(j.is_string());
    REQUIRE(j.as_string().data() == s.data());
    REQUIRE(j.as_string() == std::string_view(s));

    // The explicit owned spelling for a temporary source still copies.
    std::pmr::memory_resource *mr = std::pmr::new_delete_resource();
    Json owned(std::string_view(std::string(48, 'O')), mr);
    REQUIRE(owned.is_string());
    REQUIRE(owned.as_string() == std::string(48, 'O'));
}

TEST_CASE("Object: std::string key temporary is deleted (compile pins)") {
    // Negative pins: an rvalue std::string key must not reach the borrowed
    // insert/subscript entry points, at both the Object and Json boundaries.
    static_assert(!insert_key_ok<Object, std::string>::value);
    static_assert(!insert_key_ok<Object, std::pmr::string>::value);
    static_assert(!subscript_key_ok<Object, std::string>::value);
    static_assert(!subscript_key_ok<Json, std::string>::value);

    // Positive pins: literals, views and lvalues stay borrowed.
    static_assert(insert_key_ok<Object, std::string_view>::value);
    static_assert(insert_key_ok<Object, const char *>::value);
    static_assert(insert_key_ok<Object, std::string &>::value);
    static_assert(insert_key_ok<Object, const std::string &>::value);
    static_assert(subscript_key_ok<Object, std::string_view>::value);
    static_assert(subscript_key_ok<Object, const char *>::value);
    static_assert(subscript_key_ok<Json, const char *>::value);
    static_assert(subscript_key_ok<Json, std::string_view>::value);

    // Array::of forwarding: rvalue std::string removed, string_view kept.
    static_assert(!array_of_ok<std::string>::value);
    static_assert(array_of_ok<std::string_view>::value);

    // Runtime positive: literal and live-lvalue keys stay zero-copy.
    Object o;
    o.insert("lit", Json((int64_t)1));
    o["lit2"] = Json((int64_t)2);
    std::string k("live-key");
    o.insert(k, Json((int64_t)3));
    REQUIRE(o.begin()->first.is_owned() == false);
    REQUIRE(o.contains("live-key"));
    REQUIRE(o.at("lit") == (int64_t)1);
    REQUIRE(o.at("lit2") == (int64_t)2);
    REQUIRE(o.at("live-key") == (int64_t)3);
}
