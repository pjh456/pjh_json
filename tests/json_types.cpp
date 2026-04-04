#include <iostream>
#include <cassert>
#include <stdexcept>
#include <pjh_json/json.hpp>

using namespace pjh::json;

void simple_value()
{
    std::cout << "Json Value test started." << std::endl;

    Json null_val(nullptr);
    assert(null_val.is_null());

    Json bool_val(true);
    assert(bool_val.is_boolean());
    assert(bool_val.as_boolean());
    assert(bool_val == true);

    Json int_val((int64_t)12);
    assert(int_val.is_int());
    assert(int_val.as_int() == (int64_t)12);
    assert(int_val == (int64_t)12);

    Json float_val((double)1.2);
    assert(float_val.is_float());
    assert(float_val.as_float() == (double)1.2);
    assert(float_val == (double)1.2);

    Json str_val("str");
    assert(str_val.is_string());
    assert(str_val.as_string() == "str");
    assert(str_val == "str");

    std::cout << "Json Value test passed." << std::endl;
}

void array_value()
{
    std::cout << "Json Array test started." << std::endl;

    auto arr1 = std::move(make_array({}));
    assert(arr1.empty());
    assert(arr1.is_array());
    assert(arr1.size() == 0);
    try
    {
        auto val = arr1.at(0);
        assert(false);
    }
    catch (...)
    {
    }

    auto arr2 = std::move(make_array({make_str("pjh"), make_int(123)}));
    assert(!arr2.empty());
    assert(arr1.is_array());
    assert(arr2.size() == 2);
    assert(arr2[0] == "pjh" && arr2[1] == (int64_t)123);

    // 不同长度的 Array 比较应当为 false
    auto arr3 = std::move(make_array({make_int(1)}));
    assert(arr2 != arr3);

    // erase 越界应当抛异常
    try
    {
        arr2.as_array().erase(5);
        assert(false);
    }
    catch (const std::out_of_range &)
    {
    }

    std::cout << "Json Array test passed." << std::endl;
}

void object_value()
{
    std::cout << "Json Object test started." << std::endl;

    auto obj1 = std::move(make_object({}));
    assert(obj1.empty());
    assert(obj1.is_object());
    assert(obj1.size() == 0);

    auto obj2 = std::move(make_object({{"pjh", make_int(123)}, {"123", make_str("pjh")}}));
    assert(!obj2.empty());
    assert(obj2.is_object());
    assert(obj2.size() == 2);
    assert(obj2["pjh"] == (int64_t)123 && obj2["123"] == "pjh");

    // 不同长度的 Object 比较应当为 false
    auto obj3 = std::move(make_object({{"only", make_int(1)}}));
    assert(obj2 != obj3);

    std::cout << "Json Object test passed." << std::endl;
}

int main()
{
    simple_value();
    array_value();
    object_value();
}
