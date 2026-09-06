#include <doctest/doctest.h>
#include <pjh_json/json.hpp>
#include <pjh_json/document.hpp>
#include <pjh_json/path.hpp>
#include <pjh_json/writer.hpp>
#include <limits>
#include <memory_resource>

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

    protected:
        void *do_allocate(std::size_t n, std::size_t align) override
        {
            ++m_outstanding;
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
        size_t m_last_bytes = 0;
    };
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
    auto doc = parse_copy(R"({"a":{"b":[10,20]}})");
    auto &root = doc.root();
    REQUIRE(root.contains("a.b[1]") == true);
    REQUIRE(root.contains("a.b[9]") == false); // OOB
    REQUIRE(root.contains("z") == false); // missing key
    REQUIRE(root.contains("") == true); // empty path always true
    REQUIRE(root.contains(Path{}) == true); // typed overload

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
        REQUIRE(cr.outstanding() == 1);        // exactly one buffer allocation
        REQUIRE(cr.last_bytes() >= sv.size()); // host allocates sv.size(); >= is the portable pin
    } // ~j -> buffer deallocates back into cr
    REQUIRE(cr.outstanding() == 0);            // return proof (destroy() through res)

    // Empty sv edge (same case): SSO => zero res allocation
    Json e(std::string_view{}, &cr);
    REQUIRE(e.is_string());
    REQUIRE(e.as_string().empty());
    REQUIRE(cr.outstanding() == 0);
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
