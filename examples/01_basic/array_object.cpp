#include "pjh_json/json.hpp"
#include "pjh_json/writer.hpp"
#include <iostream>

int main()
{
    using namespace pjh::json;

    // Build array with Array::of()
    Array arr = Array::of(1, 2, 3, "four", true, nullptr);
    std::cout << "array size: " << arr.size() << "\n";

    // Iterate
    for (const auto &v : arr)
        std::cout << "  " << dump(v) << "\n";

    // Build object with Object::of()
    Object obj = Object::of(
        Object::Entry{"name", "test"},
        Object::Entry{"count", 42},
        Object::Entry{"tags", Array::of("a", "b", "c")});

    // Key access
    std::cout << "name: " << dump(obj["name"]) << "\n";
    std::cout << "count: " << obj["count"].as_int() << "\n";

    // Nested array
    auto &tags = obj["tags"].as_array();
    for (const auto &t : tags)
        std::cout << "  tag: " << dump(t) << "\n";

    // Insert new key
    obj.insert("active", true);
    std::cout << "active: " << obj["active"].as_boolean() << "\n";

    // Wrap in document and dump
    Json root(std::move(obj));
    std::cout << dump(root, DumpOptions{.pretty = true}) << "\n";
}
