// Must NOT compile: borrowing a std::string temporary would dangle.
#include <pjh_json/json.hpp>
#include <string>

void negative_case()
{
    pjh::json::Json j(std::string("temporary"));
}
