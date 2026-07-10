#include "pjh_json/json.hpp"
#include "pjh_json/parser.hpp"
#include "pjh_json/writer.hpp"
#include <iostream>

int main()
{
    std::string_view json = R"({"z":1,"a":{"nested":true,"arr":[1,2]},"m":"hello"})";
    auto doc = pjh::json::parse_copy(json);

    // Compact (default)
    std::cout << "compact: " << pjh::json::dump(doc) << "\n\n";

    // Pretty print
    std::cout << "pretty:\n"
              << pjh::json::dump(doc, {.pretty = true, .indent = 4}) << "\n\n";

    // Sorted keys
    std::cout << "sorted pretty:\n"
              << pjh::json::dump(doc, {.pretty = true, .sort_keys = true}) << "\n\n";

    // JSONL
    auto arr = pjh::json::Array::of(1, "two", false, nullptr);
    std::cout << "jsonl:\n"
              << pjh::json::dump_jsonl(arr) << "\n";

    // Prettify (parse + re-serialize)
    auto pretty = pjh::json::prettify(json, 2);
    std::cout << "prettify:\n"
              << pretty << "\n";
}
