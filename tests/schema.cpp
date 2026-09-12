#include <doctest/doctest.h>

#include <pjh_result/diagnostic.hpp>
#include <pjh_result/result.hpp>

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

#include "pjh_json/document.hpp"
#include "pjh_json/error.hpp"
#include "pjh_json/json.hpp"
#include "pjh_json/json_constexpr.hpp"
#include "pjh_json/schema.hpp"

using namespace pjh::json;

static_assert(pjh::result::Diagnostic<SchemaError>);
static_assert(pjh::result::Diagnostic<pjh::result::Context<SchemaError>>);

namespace
{
    // Parse a JSON literal into a standalone owned Json (global resource).
    Json parse_json(const char *text)
    {
        Document doc = parse_copy(text);
        return doc.root().clone(Config::instance().resource());
    }
}

TEST_CASE("Schema: type keywords")
{
    constexpr auto null_schema = ConstJson::of(kv("type", "null"));
    constexpr auto bool_schema = ConstJson::of(kv("type", "boolean"));
    constexpr auto integer_schema = ConstJson::of(kv("type", "integer"));
    constexpr auto number_schema = ConstJson::of(kv("type", "number"));
    constexpr auto string_schema = ConstJson::of(kv("type", "string"));
    constexpr auto array_schema = ConstJson::of(kv("type", "array"));
    constexpr auto object_schema = ConstJson::of(kv("type", "object"));

    const Json j_null = parse_json("null");
    const Json j_true = parse_json("true");
    const Json j_int = parse_json("2");
    const Json j_float = parse_json("2.0");
    const Json j_string = parse_json(R"("s")");
    const Json j_array = parse_json("[1,2]");
    const Json j_object = parse_json(R"({"a":1})");

    CHECK(schema::validate_result(null_schema, j_null).is_ok());
    CHECK(schema::validate_result(null_schema, j_int).is_err());
    CHECK(schema::validate_result(bool_schema, j_true).is_ok());
    CHECK(schema::validate_result(bool_schema, j_string).is_err());
    CHECK(schema::validate_result(string_schema, j_string).is_ok());
    CHECK(schema::validate_result(string_schema, j_int).is_err());
    CHECK(schema::validate_result(array_schema, j_array).is_ok());
    CHECK(schema::validate_result(array_schema, j_object).is_err());
    CHECK(schema::validate_result(object_schema, j_object).is_ok());
    CHECK(schema::validate_result(object_schema, j_array).is_err());

    // "number" accepts Integer and Floating; "integer" is tag-based, so a
    // Floating node (even an integral value like 2.0) is rejected. This is a
    // documented deviation from the mathematical JSON Schema definition.
    CHECK(schema::validate_result(number_schema, j_int).is_ok());
    CHECK(schema::validate_result(number_schema, j_float).is_ok());
    CHECK(schema::validate_result(number_schema, j_string).is_err());
    CHECK(schema::validate_result(integer_schema, j_int).is_ok());
    CHECK(schema::validate_result(integer_schema, j_float).is_err());
    CHECK(schema::validate_result(integer_schema, j_string).is_err());

    // An empty schema object accepts every instance.
    constexpr ConstJsonObject<> empty_schema{};
    CHECK(schema::validate_result(empty_schema, j_null).is_ok());
    CHECK(schema::validate_result(empty_schema, j_object).is_ok());
    CHECK(schema::validate_result(empty_schema, j_array).is_ok());
}

TEST_CASE("Schema: object required")
{
    constexpr auto sch = ConstJson::of(kv("type", "object"), kv("required", ConstJson::of("id", "name")));

    const Json good = parse_json(R"({"id":1,"name":"a","extra":true})");
    CHECK(schema::validate_result(sch, good).is_ok());

    // Missing nested/last key: path is the missing property.
    const Json missing_name = parse_json(R"({"id":1})");
    {
        const auto r = schema::validate_result(sch, missing_name);
        REQUIRE(r.is_err());
        const SchemaError &e = r.unwrap_err();
        CHECK(e.code() == SchemaErrorKind::MissingRequired);
        CHECK(e.path() == "name");
        CHECK(e.expected() == "present");
        CHECK(e.actual() == "missing");
        CHECK(e.keyword() == "required");
        CHECK(e.category() == Category::Schema);
        CHECK(e.kind() == Category::Schema);
    }

    // First failure wins: both keys are missing, the first authored one is reported.
    {
        const auto r = schema::validate_result(sch, parse_json("{}"));
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::MissingRequired);
        CHECK(r.unwrap_err().path() == "id");
    }

    // Outside its object domain the keyword is ignored.
    constexpr auto required_only = ConstJson::of(kv("required", ConstJson::of("id")));
    CHECK(schema::validate_result(required_only, parse_json("[1,2]")).is_ok());
    CHECK(schema::validate_result(required_only, parse_json("42")).is_ok());
}

TEST_CASE("Schema: object properties")
{
    constexpr auto sch = ConstJson::of(
        kv("type", "object"), kv("properties", ConstJson::of(kv("id", ConstJson::of(kv("type", "integer"))),
                                                             kv("name", ConstJson::of(kv("type", "string"))))));

    // Undeclared keys are allowed by default.
    const Json good = parse_json(R"({"id":1,"name":"x","undeclared":[1,2]})");
    CHECK(schema::validate_result(sch, good).is_ok());
    CHECK(schema::validate_result(sch, parse_json("{}")).is_ok());

    const Json bad = parse_json(R"({"id":"1","name":"x"})");
    {
        const auto r = schema::validate_result(sch, bad);
        REQUIRE(r.is_err());
        const SchemaError &e = r.unwrap_err();
        CHECK(e.code() == SchemaErrorKind::TypeMismatch);
        CHECK(e.path() == "id");
        CHECK(e.expected() == "integer");
        CHECK(e.actual() == "string");
        CHECK(e.keyword() == "type");
    }

    // First failing property in authored order short-circuits.
    {
        const auto r = schema::validate_result(sch, parse_json(R"({"id":"x","name":5})"));
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().path() == "id");
    }

    // Outside its object domain the keyword is ignored (schema without `type`).
    constexpr auto props_only =
        ConstJson::of(kv("properties", ConstJson::of(kv("id", ConstJson::of(kv("type", "integer"))))));
    CHECK(schema::validate_result(props_only, parse_json("[1]")).is_ok());
    CHECK(schema::validate_result(props_only, parse_json("7")).is_ok());
}

TEST_CASE("Schema: array items")
{
    constexpr auto sch = ConstJson::of(kv("type", "array"), kv("items", ConstJson::of(kv("type", "string"))));

    CHECK(schema::validate_result(sch, parse_json("[]")).is_ok());
    CHECK(schema::validate_result(sch, parse_json(R"(["a","b"])")).is_ok());

    const Json bad = parse_json(R"(["a",5,"c"])");
    {
        const auto r = schema::validate_result(sch, bad);
        REQUIRE(r.is_err());
        const SchemaError &e = r.unwrap_err();
        CHECK(e.code() == SchemaErrorKind::TypeMismatch);
        CHECK(e.path() == "[1]");
        CHECK(e.expected() == "string");
        CHECK(e.actual() == "integer");
        CHECK(e.keyword() == "type");
    }

    // Outside its array domain `items` is ignored.
    constexpr auto items_only = ConstJson::of(kv("items", ConstJson::of(kv("type", "string"))));
    CHECK(schema::validate_result(items_only, parse_json(R"({"a":1})")).is_ok());
    CHECK(schema::validate_result(items_only, parse_json("7")).is_ok());
    CHECK(schema::validate_result(items_only, parse_json(R"([1])")).is_err());
}

TEST_CASE("Schema: type first ordering")
{
    // `type` is evaluated before `properties` even though it is authored last.
    constexpr auto sch = ConstJson::of(kv("properties", ConstJson::of(kv("x", ConstJson::of(kv("type", "string"))))),
                                       kv("type", "array"));

    const Json obj = parse_json(R"({"x":1})");
    const auto r = schema::validate_result(sch, obj);
    REQUIRE(r.is_err());
    const SchemaError &e = r.unwrap_err();
    CHECK(e.code() == SchemaErrorKind::TypeMismatch);
    CHECK(e.path().empty()); // root: no descent into properties
    CHECK(e.expected() == "array");
    CHECK(e.actual() == "object");
    CHECK(e.keyword() == "type");
}

TEST_CASE("Schema: invalid schema")
{
    const Json j_null = parse_json("null");
    const Json j_object = parse_json("{}");
    const Json j_array = parse_json("[]");

    // Unknown type name.
    {
        constexpr auto sch = ConstJson::of(kv("type", "interger"));
        const auto r = schema::validate_result(sch, j_null);
        REQUIRE(r.is_err());
        const SchemaError &e = r.unwrap_err();
        CHECK(e.code() == SchemaErrorKind::InvalidSchema);
        CHECK(e.keyword() == "type");
        CHECK(e.actual() == "interger");
    }
    // `type` value is not a string.
    {
        constexpr auto sch = ConstJson::of(kv("type", 5));
        const auto r = schema::validate_result(sch, j_null);
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::InvalidSchema);
        CHECK(r.unwrap_err().keyword() == "type");
    }
    // `required` is not an array.
    {
        constexpr auto sch = ConstJson::of(kv("required", "id"));
        const auto r = schema::validate_result(sch, j_object);
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::InvalidSchema);
        CHECK(r.unwrap_err().keyword() == "required");
    }
    // `required` element is not a string.
    {
        constexpr auto sch = ConstJson::of(kv("required", ConstJson::of("id", 5)));
        const auto r = schema::validate_result(sch, j_object);
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::InvalidSchema);
        CHECK(r.unwrap_err().keyword() == "required");
    }
    // `properties` is not an object schema.
    {
        constexpr auto sch = ConstJson::of(kv("properties", "x"));
        const auto r = schema::validate_result(sch, j_object);
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::InvalidSchema);
        CHECK(r.unwrap_err().keyword() == "properties");
    }
    // `items` is not an object schema.
    {
        constexpr auto sch = ConstJson::of(kv("items", "x"));
        const auto r = schema::validate_result(sch, j_array);
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::InvalidSchema);
        CHECK(r.unwrap_err().keyword() == "items");
    }
    // A non-object schema value is not a schema at all.
    {
        constexpr auto sch = ConstJson::of("a", "b"); // ConstJsonArray
        const auto r = schema::validate_result(sch, j_null);
        REQUIRE(r.is_err());
        const SchemaError &e = r.unwrap_err();
        CHECK(e.code() == SchemaErrorKind::InvalidSchema);
        CHECK(e.keyword() == "schema");
    }
    // A scalar is likewise not a schema node.
    {
        constexpr auto sch = ConstJson::of(5); // ConstJsonArray<ConstJsonInt>
        const auto r = schema::validate_result(sch, j_null);
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::InvalidSchema);
    }
}

TEST_CASE("Schema: result primitive and throwing shell")
{
    constexpr auto sch = ConstJson::of(kv("type", "integer"));
    const Json good = parse_json("1");
    const Json bad = parse_json("true");

    CHECK(schema::validate_result(sch, good).is_ok());
    REQUIRE(schema::validate_result(sch, bad).is_err());

    REQUIRE_NOTHROW(schema::validate(sch, good));
    REQUIRE_THROWS_AS(schema::validate(sch, bad), SchemaError);

    // The thrown type is catchable as the JsonError base and reports Schema.
    bool caught = false;
    try
    {
        schema::validate(sch, bad);
    }
    catch (const JsonError &e)
    {
        caught = true;
        CHECK(e.category() == Category::Schema);
    }
    CHECK(caught);

    CHECK(schema_error_kind_name(SchemaErrorKind::TypeMismatch) == "TypeMismatch");
    CHECK(schema_error_kind_name(SchemaErrorKind::MissingRequired) == "MissingRequired");
    CHECK(schema_error_kind_name(SchemaErrorKind::InvalidSchema) == "InvalidSchema");
}

TEST_CASE("Schema: diagnostic")
{
    constexpr auto sch = ConstJson::of(kv("type", "integer"));
    const Json bad = parse_json(R"("x")");

    const auto r = schema::validate_result(sch, bad);
    REQUIRE(r.is_err());
    const SchemaError &e = r.unwrap_err();
    CHECK(e.kind() == Category::Schema);

    // render() is the shared Diagnostic channel; only fragments are pinned
    // (the full message text is an implementation detail, not a contract).
    const std::string rendered = pjh::result::render(e);
    CHECK(rendered.find("schema type mismatch") != std::string::npos);
    CHECK(rendered.find("<root>") != std::string::npos);
    CHECK(rendered.find("integer") != std::string::npos);
    CHECK(std::string(e.message()) == rendered);

    // Context chain end-to-end: Err -> .context(...) -> unwrap_err -> render.
    const auto ctx = schema::validate_result(sch, bad).context("validate user").unwrap_err();
    const std::string rendered_ctx = pjh::result::render(ctx);
    CHECK(rendered_ctx.rfind("validate user: ", 0) == 0);
    CHECK(rendered_ctx.find("schema type mismatch") != std::string::npos);
}

TEST_CASE("Schema: recursion")
{
    constexpr auto id_schema = ConstJson::of(kv("type", "integer"));
    constexpr auto item_schema =
        ConstJson::of(kv("type", "object"), kv("properties", ConstJson::of(kv("id", id_schema))));
    constexpr auto tags_schema = ConstJson::of(kv("type", "array"), kv("items", item_schema));
    constexpr auto user_schema = ConstJson::of(kv("type", "object"), kv("required", ConstJson::of("tags")),
                                               kv("properties", ConstJson::of(kv("tags", tags_schema))));
    constexpr auto sch = ConstJson::of(kv("type", "object"), kv("properties", ConstJson::of(kv("user", user_schema))));

    const Json good = parse_json(R"({"user":{"tags":[{"id":1},{"id":2}]}})");
    CHECK(schema::validate_result(sch, good).is_ok());

    // Failure deep in the tree carries the full path.
    const Json bad = parse_json(R"({"user":{"tags":[{"id":1},{"id":"x"}]}})");
    {
        const auto r = schema::validate_result(sch, bad);
        REQUIRE(r.is_err());
        const SchemaError &e = r.unwrap_err();
        CHECK(e.code() == SchemaErrorKind::TypeMismatch);
        CHECK(e.path() == "user.tags[1].id");
        CHECK(e.expected() == "integer");
        CHECK(e.actual() == "string");
    }

    // A missing required key is located relative to its parent object.
    const Json missing = parse_json(R"({"user":{}})");
    {
        const auto r = schema::validate_result(sch, missing);
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::MissingRequired);
        CHECK(r.unwrap_err().path() == "user.tags");
    }
}

TEST_CASE("Schema: unknown keyword ignored")
{
    constexpr auto sch =
        ConstJson::of(kv("title", "user schema"), kv("description", "unknown keyword annotation"), kv("type", "object"),
                      kv("properties", ConstJson::of(kv("id", ConstJson::of(kv("type", "integer"))))));

    CHECK(schema::validate_result(sch, parse_json(R"({"id":1})")).is_ok());

    // Known keywords still run; unknown annotations do not mask a violation.
    const auto r = schema::validate_result(sch, parse_json(R"({"id":"x"})"));
    REQUIRE(r.is_err());
    CHECK(r.unwrap_err().code() == SchemaErrorKind::TypeMismatch);
    CHECK(r.unwrap_err().path() == "id");
}

TEST_CASE("Schema: enum")
{
    constexpr auto sch = ConstJson::of(kv("enum", ConstJson::of(1, 2, "x")));

    CHECK(schema::validate_result(sch, parse_json("1")).is_ok());
    CHECK(schema::validate_result(sch, parse_json("2")).is_ok());
    CHECK(schema::validate_result(sch, parse_json(R"("x")")).is_ok());

    {
        const auto r = schema::validate_result(sch, parse_json("3"));
        REQUIRE(r.is_err());
        const SchemaError &e = r.unwrap_err();
        CHECK(e.code() == SchemaErrorKind::EnumMismatch);
        CHECK(e.path().empty());
        CHECK(e.keyword() == "enum");
        CHECK(e.category() == Category::Schema);
    }

    // Tag-strict equality: an Integer 1 is not a Floating 1.0, nor a boolean.
    {
        const auto r = schema::validate_result(sch, parse_json("1.0"));
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::EnumMismatch);
    }
    {
        const auto r = schema::validate_result(sch, parse_json("true"));
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::EnumMismatch);
    }

    // Nested failure carries the full path.
    constexpr auto color_sch = ConstJson::of(
        kv("type", "object"),
        kv("properties", ConstJson::of(kv("color", ConstJson::of(kv("enum", ConstJson::of("red", "green")))))));
    CHECK(schema::validate_result(color_sch, parse_json(R"({"color":"green"})")).is_ok());
    {
        const auto r = schema::validate_result(color_sch, parse_json(R"({"color":"blue"})"));
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::EnumMismatch);
        CHECK(r.unwrap_err().path() == "color");
        CHECK(r.unwrap_err().keyword() == "enum");
    }

    // Container members are compared structurally.
    constexpr auto arr_sch = ConstJson::of(kv("enum", ConstJson::of(ConstJson::of(1, 2))));
    CHECK(schema::validate_result(arr_sch, parse_json("[1,2]")).is_ok());
    CHECK(schema::validate_result(arr_sch, parse_json("[2,1]")).is_err());
}

TEST_CASE("Schema: const")
{
    constexpr auto int_sch = ConstJson::of(kv("const", 5));
    CHECK(schema::validate_result(int_sch, parse_json("5")).is_ok());
    {
        const auto r = schema::validate_result(int_sch, parse_json("6"));
        REQUIRE(r.is_err());
        const SchemaError &e = r.unwrap_err();
        CHECK(e.code() == SchemaErrorKind::ConstMismatch);
        CHECK(e.path().empty());
        CHECK(e.keyword() == "const");
    }
    // Tag-strict: neither a Floating 5.0 nor the string "5" matches.
    CHECK(schema::validate_result(int_sch, parse_json("5.0")).is_err());
    CHECK(schema::validate_result(int_sch, parse_json(R"("5")")).is_err());

    // String / null consts.
    constexpr auto str_sch = ConstJson::of(kv("const", "hello"));
    CHECK(schema::validate_result(str_sch, parse_json(R"("hello")")).is_ok());
    CHECK(schema::validate_result(str_sch, parse_json(R"("world")")).is_err());

    constexpr auto null_sch = ConstJson::of(kv("const", nullptr));
    CHECK(schema::validate_result(null_sch, parse_json("null")).is_ok());
    CHECK(schema::validate_result(null_sch, parse_json("0")).is_err());

    // Container const is compared structurally.
    constexpr auto obj_sch = ConstJson::of(kv("const", ConstJson::of(kv("a", 1))));
    CHECK(schema::validate_result(obj_sch, parse_json(R"({"a":1})")).is_ok());
    CHECK(schema::validate_result(obj_sch, parse_json(R"({"a":2})")).is_err());
    CHECK(schema::validate_result(obj_sch, parse_json(R"({"b":1})")).is_err());

    // `type` still runs first.
    constexpr auto typed_sch = ConstJson::of(kv("type", "integer"), kv("const", 5));
    {
        const auto r = schema::validate_result(typed_sch, parse_json("true"));
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::TypeMismatch);
    }
}

TEST_CASE("Schema: string length")
{
    constexpr auto min_sch = ConstJson::of(kv("minLength", 3));
    CHECK(schema::validate_result(min_sch, parse_json(R"("abc")")).is_ok());
    CHECK(schema::validate_result(min_sch, parse_json(R"("abcd")")).is_ok());
    {
        const auto r = schema::validate_result(min_sch, parse_json(R"("ab")"));
        REQUIRE(r.is_err());
        const SchemaError &e = r.unwrap_err();
        CHECK(e.code() == SchemaErrorKind::TooShort);
        CHECK(e.keyword() == "minLength");
        CHECK(e.path().empty());
    }

    constexpr auto max_sch = ConstJson::of(kv("maxLength", 3));
    CHECK(schema::validate_result(max_sch, parse_json(R"("abc")")).is_ok());
    {
        const auto r = schema::validate_result(max_sch, parse_json(R"("abcd")"));
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::TooLong);
        CHECK(r.unwrap_err().keyword() == "maxLength");
    }

    // Inclusive zero boundaries and the empty string.
    constexpr auto min0 = ConstJson::of(kv("minLength", 0));
    constexpr auto max0 = ConstJson::of(kv("maxLength", 0));
    CHECK(schema::validate_result(min0, parse_json(R"("")")).is_ok());
    CHECK(schema::validate_result(max0, parse_json(R"("")")).is_ok());
    CHECK(schema::validate_result(max0, parse_json(R"("a")")).is_err());

    // Unicode code points, not bytes: "héllo" is 5 code points / 6 bytes and
    // "😀" (U+1F600) is 1 code point / 4 bytes.
    constexpr auto min5 = ConstJson::of(kv("minLength", 5));
    constexpr auto min6 = ConstJson::of(kv("minLength", 6));
    const Json hello = parse_json(R"("héllo")");
    CHECK(schema::validate_result(min5, hello).is_ok());
    CHECK(schema::validate_result(min6, hello).is_err());

    constexpr auto min1 = ConstJson::of(kv("minLength", 1));
    constexpr auto min2 = ConstJson::of(kv("minLength", 2));
    const Json emoji = parse_json(R"("😀")");
    CHECK(schema::validate_result(min1, emoji).is_ok());
    CHECK(schema::validate_result(min2, emoji).is_err());

    // Outside its string domain the keyword is ignored.
    CHECK(schema::validate_result(min_sch, parse_json("[1,2]")).is_ok());
    CHECK(schema::validate_result(min_sch, parse_json("42")).is_ok());

    // Nested path.
    constexpr auto nested = ConstJson::of(
        kv("type", "object"), kv("properties", ConstJson::of(kv("name", ConstJson::of(kv("minLength", 3))))));
    {
        const auto r = schema::validate_result(nested, parse_json(R"({"name":"ab"})"));
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().path() == "name");
        CHECK(r.unwrap_err().code() == SchemaErrorKind::TooShort);
    }
}

TEST_CASE("Schema: array item bounds")
{
    constexpr auto min_sch = ConstJson::of(kv("minItems", 2));
    CHECK(schema::validate_result(min_sch, parse_json("[1,2]")).is_ok());
    CHECK(schema::validate_result(min_sch, parse_json("[1,2,3]")).is_ok());
    {
        const auto r = schema::validate_result(min_sch, parse_json("[1]"));
        REQUIRE(r.is_err());
        const SchemaError &e = r.unwrap_err();
        CHECK(e.code() == SchemaErrorKind::TooShort);
        CHECK(e.keyword() == "minItems");
        CHECK(e.path().empty());
    }
    CHECK(schema::validate_result(min_sch, parse_json("[]")).is_err());

    constexpr auto max_sch = ConstJson::of(kv("maxItems", 2));
    CHECK(schema::validate_result(max_sch, parse_json("[]")).is_ok());
    CHECK(schema::validate_result(max_sch, parse_json("[1,2]")).is_ok());
    {
        const auto r = schema::validate_result(max_sch, parse_json("[1,2,3]"));
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::TooLong);
        CHECK(r.unwrap_err().keyword() == "maxItems");
    }

    // maxItems 0 admits only the empty array.
    constexpr auto max0 = ConstJson::of(kv("maxItems", 0));
    CHECK(schema::validate_result(max0, parse_json("[]")).is_ok());
    CHECK(schema::validate_result(max0, parse_json("[1]")).is_err());

    // Domain-ignore outside arrays.
    CHECK(schema::validate_result(min_sch, parse_json(R"("ab")")).is_ok());
    CHECK(schema::validate_result(min_sch, parse_json("{}")).is_ok());

    // Interplay with `items` follows authored order.
    constexpr auto both = ConstJson::of(kv("items", ConstJson::of(kv("type", "integer"))), kv("minItems", 2));
    CHECK(schema::validate_result(both, parse_json("[1,2]")).is_ok());
    {
        const auto r = schema::validate_result(both, parse_json("[1]"));
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::TooShort);
    }
    {
        const auto r = schema::validate_result(both, parse_json(R"(["x"])"));
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::TypeMismatch);
        CHECK(r.unwrap_err().path() == "[0]");
    }
}

TEST_CASE("Schema: numeric bounds")
{
    constexpr auto min_sch = ConstJson::of(kv("minimum", 3));
    CHECK(schema::validate_result(min_sch, parse_json("3")).is_ok());
    CHECK(schema::validate_result(min_sch, parse_json("4")).is_ok());
    CHECK(schema::validate_result(min_sch, parse_json("3.5")).is_ok());
    {
        const auto r = schema::validate_result(min_sch, parse_json("2"));
        REQUIRE(r.is_err());
        const SchemaError &e = r.unwrap_err();
        CHECK(e.code() == SchemaErrorKind::BelowMinimum);
        CHECK(e.keyword() == "minimum");
        CHECK(e.path().empty());
    }
    CHECK(schema::validate_result(min_sch, parse_json("2.9")).is_err());

    constexpr auto max_sch = ConstJson::of(kv("maximum", 3));
    CHECK(schema::validate_result(max_sch, parse_json("3")).is_ok());
    CHECK(schema::validate_result(max_sch, parse_json("3.0")).is_ok());
    CHECK(schema::validate_result(max_sch, parse_json("-1")).is_ok());
    {
        const auto r = schema::validate_result(max_sch, parse_json("4"));
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::AboveMaximum);
        CHECK(r.unwrap_err().keyword() == "maximum");
    }
    CHECK(schema::validate_result(max_sch, parse_json("4.1")).is_err());

    // Floating bounds against integer and floating instances.
    constexpr auto fmin = ConstJson::of(kv("minimum", 2.5));
    CHECK(schema::validate_result(fmin, parse_json("2.5")).is_ok());
    CHECK(schema::validate_result(fmin, parse_json("3")).is_ok());
    CHECK(schema::validate_result(fmin, parse_json("2")).is_err());

    // Negative bounds are ordinary numbers.
    constexpr auto neg = ConstJson::of(kv("minimum", -2));
    CHECK(schema::validate_result(neg, parse_json("-2")).is_ok());
    CHECK(schema::validate_result(neg, parse_json("-3")).is_err());

    // Both bounds, first failure in authored order.
    constexpr auto range = ConstJson::of(kv("minimum", 1), kv("maximum", 10));
    CHECK(schema::validate_result(range, parse_json("1")).is_ok());
    CHECK(schema::validate_result(range, parse_json("10")).is_ok());
    CHECK(schema::validate_result(range, parse_json("0")).is_err());
    CHECK(schema::validate_result(range, parse_json("11")).is_err());

    // Domain-ignore outside numbers.
    CHECK(schema::validate_result(min_sch, parse_json(R"("3")")).is_ok());
    CHECK(schema::validate_result(min_sch, parse_json("true")).is_ok());

    // Int64 precision is preserved when both sides are integers.
    constexpr auto imin = ConstJson::of(kv("minimum", INT64_C(999999999999999998)));
    CHECK(schema::validate_result(imin, parse_json("999999999999999999")).is_ok());
    CHECK(schema::validate_result(imin, parse_json("999999999999999997")).is_err());

    // `type` still runs first.
    constexpr auto typed = ConstJson::of(kv("minimum", 5), kv("type", "integer"));
    {
        const auto r = schema::validate_result(typed, parse_json("4.0"));
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::TypeMismatch);
    }
}

TEST_CASE("Schema: additionalProperties")
{
    constexpr auto sch = ConstJson::of(kv("type", "object"),
                                       kv("properties", ConstJson::of(kv("id", ConstJson::of(kv("type", "integer"))))),
                                       kv("additionalProperties", false));

    CHECK(schema::validate_result(sch, parse_json(R"({"id":1})")).is_ok());
    CHECK(schema::validate_result(sch, parse_json("{}")).is_ok());
    {
        const auto r = schema::validate_result(sch, parse_json(R"({"id":1,"extra":2})"));
        REQUIRE(r.is_err());
        const SchemaError &e = r.unwrap_err();
        CHECK(e.code() == SchemaErrorKind::UnexpectedProperty);
        CHECK(e.path() == "extra");
        CHECK(e.keyword() == "additionalProperties");
        CHECK(e.category() == Category::Schema);
    }

    // `true` allows extra keys.
    constexpr auto allow = ConstJson::of(
        kv("type", "object"), kv("properties", ConstJson::of(kv("id", ConstJson::of(kv("type", "integer"))))),
        kv("additionalProperties", true));
    CHECK(schema::validate_result(allow, parse_json(R"({"id":1,"extra":2})")).is_ok());

    // Without `properties`, every key is additional.
    constexpr auto no_props = ConstJson::of(kv("additionalProperties", false));
    CHECK(schema::validate_result(no_props, parse_json("{}")).is_ok());
    CHECK(schema::validate_result(no_props, parse_json(R"({"a":1})")).is_err());

    // Nested: the failing key is appended to the parent path.
    constexpr auto nested = ConstJson::of(
        kv("type", "object"),
        kv("properties",
           ConstJson::of(kv(
               "user", ConstJson::of(kv("type", "object"),
                                     kv("properties", ConstJson::of(kv("name", ConstJson::of(kv("type", "string"))))),
                                     kv("additionalProperties", false))))));
    CHECK(schema::validate_result(nested, parse_json(R"({"user":{"name":"x"}})")).is_ok());
    {
        const auto r = schema::validate_result(nested, parse_json(R"({"user":{"name":"x","y":1}})"));
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::UnexpectedProperty);
        CHECK(r.unwrap_err().path() == "user.y");
    }

    // Authored order: additionalProperties first rejects an undeclared key
    // before `properties` recurses.
    constexpr auto ap_first =
        ConstJson::of(kv("additionalProperties", false),
                      kv("properties", ConstJson::of(kv("a", ConstJson::of(kv("type", "integer"))))));
    {
        const auto r = schema::validate_result(ap_first, parse_json(R"({"b":"x"})"));
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::UnexpectedProperty);
        CHECK(r.unwrap_err().path() == "b");
    }
    // A declared-but-ill-typed key passes the gate, then fails in `properties`.
    {
        const auto r = schema::validate_result(ap_first, parse_json(R"({"a":"x"})"));
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::TypeMismatch);
        CHECK(r.unwrap_err().path() == "a");
    }
    // `properties` first: the type failure wins over the undeclared key.
    constexpr auto props_first =
        ConstJson::of(kv("properties", ConstJson::of(kv("a", ConstJson::of(kv("type", "integer"))))),
                      kv("additionalProperties", false));
    {
        const auto r = schema::validate_result(props_first, parse_json(R"({"a":"x","b":1})"));
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::TypeMismatch);
        CHECK(r.unwrap_err().path() == "a");
    }

    // Outside its object domain the keyword is ignored.
    CHECK(schema::validate_result(no_props, parse_json("[1,2]")).is_ok());
    CHECK(schema::validate_result(no_props, parse_json("7")).is_ok());
}

TEST_CASE("Schema: value constraint invalid schema")
{
    const Json j_str = parse_json(R"("abc")");
    const Json j_arr = parse_json("[1]");
    const Json j_num = parse_json("1");
    const Json j_obj = parse_json(R"({"a":1})");

    // `enum` must be an array.
    {
        constexpr auto sch = ConstJson::of(kv("enum", "x"));
        const auto r = schema::validate_result(sch, j_num);
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::InvalidSchema);
        CHECK(r.unwrap_err().keyword() == "enum");
    }
    // Length / item bounds must be non-negative integers.
    {
        constexpr auto sch = ConstJson::of(kv("minLength", "3"));
        const auto r = schema::validate_result(sch, j_str);
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::InvalidSchema);
        CHECK(r.unwrap_err().keyword() == "minLength");
    }
    {
        constexpr auto sch = ConstJson::of(kv("maxLength", -1));
        const auto r = schema::validate_result(sch, j_str);
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::InvalidSchema);
        CHECK(r.unwrap_err().keyword() == "maxLength");
    }
    {
        constexpr auto sch = ConstJson::of(kv("minItems", 1.5));
        const auto r = schema::validate_result(sch, j_arr);
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::InvalidSchema);
        CHECK(r.unwrap_err().keyword() == "minItems");
    }
    {
        constexpr auto sch = ConstJson::of(kv("maxItems", true));
        const auto r = schema::validate_result(sch, j_arr);
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::InvalidSchema);
        CHECK(r.unwrap_err().keyword() == "maxItems");
    }
    // Numeric bounds must be numbers.
    {
        constexpr auto sch = ConstJson::of(kv("minimum", "3"));
        const auto r = schema::validate_result(sch, j_num);
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::InvalidSchema);
        CHECK(r.unwrap_err().keyword() == "minimum");
    }
    {
        constexpr auto sch = ConstJson::of(kv("maximum", true));
        const auto r = schema::validate_result(sch, j_num);
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::InvalidSchema);
        CHECK(r.unwrap_err().keyword() == "maximum");
    }
    // `additionalProperties` must be a boolean.
    {
        constexpr auto sch = ConstJson::of(kv("additionalProperties", 5));
        const auto r = schema::validate_result(sch, j_obj);
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().code() == SchemaErrorKind::InvalidSchema);
        CHECK(r.unwrap_err().keyword() == "additionalProperties");
    }

    // schema_error_kind_name covers the value-constraint kinds.
    CHECK(schema_error_kind_name(SchemaErrorKind::UnexpectedProperty) == "UnexpectedProperty");
    CHECK(schema_error_kind_name(SchemaErrorKind::TooShort) == "TooShort");
    CHECK(schema_error_kind_name(SchemaErrorKind::TooLong) == "TooLong");
    CHECK(schema_error_kind_name(SchemaErrorKind::BelowMinimum) == "BelowMinimum");
    CHECK(schema_error_kind_name(SchemaErrorKind::AboveMaximum) == "AboveMaximum");
    CHECK(schema_error_kind_name(SchemaErrorKind::EnumMismatch) == "EnumMismatch");
    CHECK(schema_error_kind_name(SchemaErrorKind::ConstMismatch) == "ConstMismatch");
}

TEST_CASE("Schema: value constraint diagnostics")
{
    constexpr auto sch = ConstJson::of(kv("enum", ConstJson::of(1, 2)));
    const Json bad = parse_json("3");

    const auto r = schema::validate_result(sch, bad);
    REQUIRE(r.is_err());
    const SchemaError &e = r.unwrap_err();
    CHECK(e.kind() == Category::Schema);

    const std::string rendered = pjh::result::render(e);
    CHECK(rendered.find("value not in enum") != std::string::npos);
    CHECK(rendered.find("<root>") != std::string::npos);

    // Throwing shell for a value constraint, catchable as JsonError.
    REQUIRE_THROWS_AS(schema::validate(sch, bad), SchemaError);
    bool caught = false;
    try
    {
        schema::validate(sch, bad);
    }
    catch (const JsonError &ex)
    {
        caught = true;
        CHECK(ex.category() == Category::Schema);
    }
    CHECK(caught);
}
