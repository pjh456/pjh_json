#ifndef INCLUDE_PJH_JSON_PARSER_HPP
#define INCLUDE_PJH_JSON_PARSER_HPP

#include <string_view>
#include <string>
#include <stdexcept>
#include <cstdint>
#include <bit>
#include <charconv>

#include "json_fwd.hpp"

namespace pjh::json
{
    class Parser
    {
    private:
        const char *m_curr;
        const char *m_end;

    public:
        // 初始化并接收一段 JSON 文本
        explicit Parser(std::string_view json)
            : m_curr(json.data()), m_end(json.data() + json.size()) {}

        // 主入口
        Json parse();

    private:
        // 核心解析器函数
        void skip_whitespace();
        Json parse_value();
        Json parse_object();
        Json parse_array();
        std::string_view parse_string();
        Json parse_number();
        Json parse_literal();

    public:
        uint32_t parse_hex4();

        [[noreturn]] void error(const std::string &msg) const
        {
            throw std::runtime_error("JSON Parse Error: " + msg);
        }
    };
}

#endif // INCLUDE_PJH_JSON_PARSER_HPP