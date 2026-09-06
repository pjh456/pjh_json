#include <doctest/doctest.h>
#include <pjh_json/json.hpp>
#include <pjh_json/document.hpp>
#include <pjh_json/writer.hpp>
#include <memory_resource>

using namespace pjh::json;

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
    REQUIRE(dump(j) == "{}");
}
