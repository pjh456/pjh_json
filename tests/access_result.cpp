#include <doctest/doctest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include <pjh_result/diagnostic.hpp>

#include <pjh_json.hpp>

using namespace pjh::json;

static_assert(pjh::result::Diagnostic<JsonError>);
static_assert(pjh::result::Diagnostic<ParseError>);
static_assert(pjh::result::Diagnostic<TypeError>);
static_assert(pjh::result::Diagnostic<AccessError>);
static_assert(pjh::result::Diagnostic<pjh::result::Context<ParseError>>);

TEST_CASE("Access: find_path_result success") {
    auto doc = parse_copy(R"({"a":{"b":[10,20,{"c":true}]}})");
    Json &root = doc.root();

    auto r = find_path_result(root, "a.b[2].c");
    REQUIRE(r.is_ok());
    Json *p = r.unwrap();
    REQUIRE(p != nullptr);
    REQUIRE(*p == true);

    REQUIRE(*find_path_result(root, "a.b[0]").unwrap() == (int64_t)10);
    REQUIRE(find_path_result(root, "").unwrap() == &root); // empty path = root

    // Const overload returns const Json * (compile-time pin)
    const Json &cj = doc.root();
    auto cr = find_path_result(cj, "a.b[1]");
    REQUIRE(cr.is_ok());
    const Json *cp = cr.unwrap();
    REQUIRE(cp != nullptr);
    REQUIRE(*cp == (int64_t)20);
}

TEST_CASE("Access: find_path_result missing") {
    auto doc = parse_copy(R"({"a":{"b":1}})");
    auto r = find_path_result(doc.root(), "a.z");
    REQUIRE(r.is_err());
    const AccessError &e = r.unwrap_err();
    REQUIRE(e.code == AccessErrorKind::Missing);
    REQUIRE(e.kind() == AccessErrorKind::Missing);
    REQUIRE(e.path == "a.z");
    REQUIRE(e.expected == "object");
    REQUIRE(e.actual.empty());
    REQUIRE(e.hop == 1);
    REQUIRE(e.message() == "missing key: a.z");
}

TEST_CASE("Access: find_path_result out of range") {
    auto doc = parse_copy(R"({"a":[1,2,3]})");
    auto r = find_path_result(doc.root(), "a[9]");
    REQUIRE(r.is_err());
    const AccessError &e = r.unwrap_err();
    REQUIRE(e.code == AccessErrorKind::OutOfRange);
    REQUIRE(e.path == "a[9]");
    REQUIRE(e.expected == "array");
    REQUIRE(e.actual.empty());
    REQUIRE(e.hop == 1);
    REQUIRE(e.message() == "index out of range: a[9]");
}

TEST_CASE("Access: find_path_result type mismatch") {
    auto doc = parse_copy(R"({"x":5})");
    auto r = find_path_result(doc.root(), "x.y");
    REQUIRE(r.is_err());
    const AccessError &e = r.unwrap_err();
    REQUIRE(e.code == AccessErrorKind::TypeMismatch);
    REQUIRE(e.path == "x.y");
    REQUIRE(e.expected == "object");
    REQUIRE(e.actual == "integer");
    REQUIRE(e.hop == 1);
    REQUIRE(e.message() == "type mismatch at x.y: expected object, got integer");
}

TEST_CASE("Access: find_path_result invalid index") {
    auto doc = parse_copy(R"({"a":[1,2,3]})");
    auto r = find_path_result(doc.root(), "a.x");
    REQUIRE(r.is_err());
    const AccessError &e = r.unwrap_err();
    REQUIRE(e.code == AccessErrorKind::InvalidIndex);
    REQUIRE(e.path == "a.x");
    REQUIRE(e.expected == "array");
    REQUIRE(e.hop == 1);
    REQUIRE(e.message() == "invalid array index: a.x");
}

TEST_CASE("Access: find_path_result malformed path") {
    auto doc = parse_copy(R"({"a":[1,2,3]})");
    auto r = find_path_result(doc.root(), "a[3");
    REQUIRE(r.is_err());
    const AccessError &e = r.unwrap_err();
    REQUIRE(e.code == AccessErrorKind::MalformedPath);
    REQUIRE(e.path == "a[3");
    REQUIRE(e.hop == SIZE_MAX);
    REQUIRE(e.message() == "malformed path: a[3");

    // The DSL error is wrapped, never thrown (contrast find_path)
    REQUIRE_NOTHROW((void)find_path_result(doc.root(), "a[3"));
}

TEST_CASE("Access: find_path_result digit key rule") {
    auto doc = parse_copy(R"({"o":{"3":1},"a":[1,2,3]})");
    Json &root = doc.root();
    REQUIRE(*find_path_result(root, "o[3]").unwrap() == (int64_t)1);
    REQUIRE(*find_path_result(root, "o.3").unwrap() == (int64_t)1);
    REQUIRE(*find_path_result(root, "a[1]").unwrap() == (int64_t)2);
    REQUIRE(*find_path_result(root, "a.1").unwrap() == (int64_t)2);

    auto r = find_path_result(root, "a.x"); // array parent + non-digit key
    REQUIRE(r.is_err());
    REQUIRE(r.unwrap_err().code == AccessErrorKind::InvalidIndex);
}

TEST_CASE("Access: get_path scalars") {
    auto doc = parse_copy(R"({"b":true,"i":7,"f":1.5,"s":"hi"})");
    const Json &root = doc.root();

    REQUIRE(get_path<bool>(root, "b").unwrap() == true);
    REQUIRE(get_path<int64_t>(root, "i").unwrap() == (int64_t)7);
    REQUIRE(get_path<float>(root, "f").unwrap() == doctest::Approx(1.5f));
    REQUIRE(get_path<double>(root, "f").unwrap() == doctest::Approx(1.5));
    REQUIRE(get_path<std::string_view>(root, "s").unwrap() == "hi");
    // get<T>'s widening semantic: an Integer node satisfies float/double
    REQUIRE(get_path<double>(root, "i").unwrap() == doctest::Approx(7.0));

    // Typed-path overload (compile pin)
    Path p;
    p.emplace_back(std::string_view{"i"});
    REQUIRE(get_path<int64_t>(root, p).unwrap() == (int64_t)7);
}

TEST_CASE("Access: get_path type mismatch") {
    auto doc = parse_copy(R"({"s":"hi"})");
    const Json &root = doc.root();
    auto r = get_path<int64_t>(root, "s");
    REQUIRE(r.is_err());
    const AccessError &e = r.unwrap_err();
    REQUIRE(e.code == AccessErrorKind::TypeMismatch);
    REQUIRE(e.expected == "integer");
    REQUIRE(e.actual == "string");
    REQUIRE(e.path == "s");
    REQUIRE(e.hop == SIZE_MAX);
    REQUIRE(e.message() == "type mismatch at s: expected integer, got string");

    // A resolution failure is forwarded unchanged, not re-typed
    auto m = get_path<int64_t>(root, "z");
    REQUIRE(m.is_err());
    REQUIRE(m.unwrap_err().code == AccessErrorKind::Missing);
}

TEST_CASE("Access: diagnostic render") {
    REQUIRE(pjh::result::render(ParseError("bad", 3)) == "bad");
    REQUIRE(pjh::result::render(TypeError("mismatch")) == "mismatch");

    AccessError e(AccessErrorKind::TypeMismatch, "a.b", "integer", "string");
    REQUIRE(pjh::result::render(e) ==
            "type mismatch at a.b: expected integer, got string");
    REQUIRE(e.kind() == AccessErrorKind::TypeMismatch);
    REQUIRE(e.message() ==
            std::string_view("type mismatch at a.b: expected integer, got string"));

    // Context chain end-to-end: Err -> .context(...) -> unwrap_err -> render
    auto ctx = pjh::result::Result<int, AccessError>::Err(e)
                   .context("load deck")
                   .unwrap_err();
    REQUIRE(pjh::result::render(ctx) ==
            "load deck: type mismatch at a.b: expected integer, got string");
}

TEST_CASE("Access: throwing accessors unchanged") {
    auto doc = parse_copy(R"({"o":{"3":1},"a":[1,2,3]})");
    Json &root = doc.root();

    REQUIRE_THROWS_AS((void)root.at_path("a.x"), std::out_of_range);
    REQUIRE_THROWS_AS((void)root.at_path("a[9]"), std::out_of_range);
    REQUIRE_THROWS_AS((void)root.at_path("a[3"), std::invalid_argument);

    auto d2 = parse_copy(R"({"x":5})");
    REQUIRE_THROWS_AS((void)d2.root().at_path("x.y"), TypeError);
    REQUIRE_THROWS_AS((void)d2.root().at_path("x[0]"), TypeError);

    REQUIRE(root.find_path("z") == nullptr);
    REQUIRE(root.find_path("a[9]") == nullptr);
    REQUIRE(root.find_path("a.x") == nullptr);
    REQUIRE_THROWS_AS((void)root.find_path("a[3"), std::invalid_argument);

    REQUIRE_THROWS_AS((void)d2.root()["x"].get<bool>(), TypeError);
    REQUIRE_THROWS_AS((void)d2.root()["x"].at(0), TypeError);
    REQUIRE_THROWS_AS((void)d2.root()["x"].as_string_strict(), TypeError);
    REQUIRE_THROWS_AS((void)d2.root().at("missing"), std::out_of_range);
}
