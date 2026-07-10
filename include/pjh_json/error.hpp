#ifndef INCLUDE_PJH_JSON_ERROR_HPP
#define INCLUDE_PJH_JSON_ERROR_HPP

#include <stdexcept>

namespace pjh::json
{

    /**
     * @brief Base exception for all JSON errors
     */
    class JsonError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    /**
     * @brief JSON parse failure
     */
    class ParseError : public JsonError
    {
    public:
        using JsonError::JsonError;
    };

    /**
     * @brief Type mismatch on access
     */
    class TypeError : public JsonError
    {
    public:
        using JsonError::JsonError;
    };

}

#endif
