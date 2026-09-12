// Must NOT compile: get<T> accepts only bool / int64_t / float / double.
#include <pjh_json/json.hpp>
#include <cstdint>

void negative_case()
{
    pjh::json::Json j;
    (void)j.get<uint32_t>();
}
