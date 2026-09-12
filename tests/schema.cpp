#include <doctest/doctest.h>

#include <pjh_result/diagnostic.hpp>
#include <pjh_result/result.hpp>

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
    // (the full message text is an implementation detail, task 79 contract).
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
