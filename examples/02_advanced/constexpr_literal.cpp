#include "pjh_json/json.hpp"
#include "pjh_json/document.hpp"
#include "pjh_json/writer.hpp"

#include <iostream>

using namespace pjh::json;

int main()
{
    // --- constexpr scalar construction ---
    constexpr Json j_int   = 42;
    constexpr Json j_float = 3.14;
    constexpr Json j_str   = std::string_view("hello");
    constexpr Json j_bool  = true;
    constexpr Json j_null;

    std::cout << "constexpr scalars: "
              << "int=" << *j_int.try_as_int()
              << " float=" << *j_float.try_as_float()
              << " str=" << *j_str.try_as_string()
              << " bool=" << *j_bool.try_as_boolean()
              << " null=" << j_null.is_null()
              << std::endl;

    // --- constexpr array literal ---
    auto arr = json_array_literal(1, 2, std::string_view("three"), false, nullptr);
    auto runtime_arr = array_from_literal(std::move(arr));

    std::pmr::string arr_json = dump(Json(std::move(runtime_arr)));
    std::cout << "array literal: " << arr_json << std::endl;

    // --- constexpr object literal ---
    auto entries = std::array{
        kv("name", std::string_view("alice")),
        kv("age", int64_t(30)),
        kv("active", true),
    };
    auto runtime_obj = object_from_literal(std::move(entries));

    std::pmr::string obj_json = dump(Json(std::move(runtime_obj)),
                                     DumpOptions{.pretty = true});
    std::cout << "object literal:\n" << obj_json << std::endl;

    // --- constexpr String ---
    constexpr String s1 = "compile-time";
    constexpr String s2 = "compile-time";
    static_assert(s1 == s2);

    std::cout << "constexpr string: " << static_cast<std::string_view>(s1) << std::endl;

    return 0;
}
