// Documentation examples must not rot (roadmap 64). Every code block below is
// mirrored from README.md or a public header @code block. When the source doc
// changes, update the mirror in the same commit.
#include <doctest/doctest.h>

#include <pjh_json.hpp>
#include <pjh_json/json_constexpr.hpp>

#include <concepts>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory_resource>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

using namespace pjh::json;

// --- mirrors README.md "## Example" -----------------------------------------
[[maybe_unused]] static void readme_runtime_example()
{
    // Parse
    auto doc = parse_copy(R"({"name":"pjh_json","version":1})");
    const auto &root = doc.root();

    // Access
    auto ver = root["version"].try_as_int();   // std::optional<int64_t>
    auto name = root["name"].try_as_string();  // std::optional<std::string_view>

    // Build
    Object obj = Object::of(
        Object::Entry{"key", "value"},
        Object::Entry{"count", 42},
        Object::Entry{"tags", Array::of("a", "b", "c")});

    // Serialize
    std::pmr::string compact = dump(doc);
    std::pmr::string pretty = dump(doc, DumpOptions{.pretty = true, .indent = 2});
    dump_file("pjh_doc_example_tmp.json", doc.root());

    (void)ver;
    (void)name;
    (void)obj;
    (void)compact;
    (void)pretty;
}

// --- mirrors README.md "Compile-time JSON construction" + "validation" -------
[[maybe_unused]] static void readme_constexpr_example()
{
    constexpr auto n = to_const_json(42);
    static_assert(n.v == 42);
    Json jn = to_runtime(n);

    auto arr = ConstJson::of(1, 2.5, std::string_view("hello"), true, nullptr);
    Json jarr = arr.to_runtime();

    auto obj = ConstJson::of(kv("name", std::string_view("alice")),
                             kv("age", int64_t(30)),
                             kv("score", 99.5),
                             kv("active", true));
    Json jobj = obj.to_runtime();

    auto nested = ConstJson::of(
        kv("user", ConstJson::of(kv("id", 42),
                                 kv("tags", ConstJson::of("admin", "dev")))));
    Json jnested = nested.to_runtime();

    constexpr auto pr = ConstJson::parse(R"({"port":8080,"debug":false})");
    static_assert(pr.valid);
    auto doc = pr.to_document();
    auto port = doc.root()["port"].as_int();

    (void)jn;
    (void)jarr;
    (void)jobj;
    (void)jnested;
    (void)doc;
    (void)port;
}

// --- mirrors include/pjh_json/json.hpp @code (range-for) --------------------
[[maybe_unused]] static void header_range_for_example()
{
    auto doc = parse_copy(R"({"nums":[1,2,3]})");
    for (auto &&e : doc.root()["nums"])
        e.value = Json(e.value.as_int() * 2);
}

// --- mirrors include/pjh_json/json.hpp @code (visit) ------------------------
[[maybe_unused]] static std::string header_visit_example(const Json &j)
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

// --- mirrors include/pjh_json/object.hpp @code (keys) -----------------------
[[maybe_unused]] static std::size_t header_keys_example(const Object &obj)
{
    std::size_t total = 0;
    for (std::string_view k : obj.keys())
        total += k.size();
    return total;
}

TEST_CASE("Docs: README runtime example compiles and runs")
{
    readme_runtime_example();
    std::remove("pjh_doc_example_tmp.json");

    auto doc = parse_copy(R"({"name":"pjh_json","version":1})");
    REQUIRE(doc.root()["name"].as_string() == std::string_view("pjh_json"));
}

TEST_CASE("Docs: README compile-time examples compile and run")
{
    readme_constexpr_example();

    constexpr auto pr = ConstJson::parse(R"({"port":8080,"debug":false})");
    static_assert(pr.valid);
    auto doc = pr.to_document();
    REQUIRE(doc.root()["port"].as_int() == (int64_t)8080);
}

TEST_CASE("Docs: header @code examples compile and run")
{
    header_range_for_example();

    auto doc = parse_copy(R"({"n":1,"s":"x"})");
    REQUIRE(header_visit_example(doc.root()["n"]) == "1");
    REQUIRE(header_visit_example(doc.root()["s"]) == "x");

    auto obj = parse_copy(R"({"ab":1,"cde":2})");
    REQUIRE(header_keys_example(obj.root().as_object()) == (std::size_t)5);
}
