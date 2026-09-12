// Must NOT compile: Json is move-only (deep copy = clone()).
#include <pjh_json/json.hpp>

void negative_case()
{
    pjh::json::Json a;
    pjh::json::Json b = a;
}
