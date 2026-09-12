// Must NOT compile: consteval dump rejects any tree containing a double.
// C++20 std::to_chars has no constexpr floating-point overload, so the dump
// entry points static_assert on const_json_has_double_v. Use to_runtime() +
// the runtime dump() for double-bearing values.
#include <pjh_json/json_constexpr.hpp>

using namespace pjh::json;

void negative_case()
{
    (void)const_dump(ConstJson::of(1.5));
}
