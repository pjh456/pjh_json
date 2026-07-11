#include "pjh_json/json.hpp"
#include "pjh_json/json_constexpr.hpp"
#include "pjh_json/document.hpp"
#include "pjh_json/writer.hpp"

#include <iostream>

using namespace pjh::json;

int main()
{
    // compile-time array
    auto runtime_arr =
        ConstJson::of(1, 2, std::string_view("three"), false, nullptr)
            .to_runtime();
    auto arr_json = dump(runtime_arr);
    std::cout << "array: " << arr_json << std::endl;

    // compile-time object
    auto runtime_obj =
        ConstJson::of(
            kv("name", std::string_view("alice")),
            kv("age", int64_t(30)),
            kv("active", true))
            .to_runtime();
    auto obj_json =
        dump(runtime_obj, DumpOptions{.pretty = true});
    std::cout << "object:\n"
              << obj_json << std::endl;

    // nested
    auto nested =
        ConstJson::of(
            kv("user",
               ConstJson::of(
                   kv("id", 42),
                   kv("tags",
                      ConstJson::of("admin", "dev")))))
            .to_runtime();
    auto nested_json =
        dump(nested, DumpOptions{.pretty = true});
    std::cout << "nested:\n"
              << nested_json << std::endl;

    // compile-time validate
    auto pr = ConstJson::parse(R"({"status":"ok","count":5})");
    if (pr.valid)
    {
        auto doc = pr.to_document();
        std::cout << "parse: "
                  << doc.root()["status"]
                         .as_string()
                  << std::endl;
    }

    constexpr String s1 = "compile-time";
    constexpr String s2 = "compile-time";
    static_assert(s1 == s2);
    std::cout << "string: "
              << static_cast<std::string_view>(s1)
              << std::endl;

    return 0;
}
