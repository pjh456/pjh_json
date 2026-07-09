#ifndef INCLUDE_PJH_JSON_PARSER_HPP
#define INCLUDE_PJH_JSON_PARSER_HPP

#include <string_view>
#include <string>
#include <stdexcept>
#include <cstdint>
#include <bit>
#include <charconv>
#include <memory_resource>

#include "document.hpp"

namespace pjh::json
{
    class Parser
    {
    private:
        const char *m_curr;
        const char *m_end;
        std::pmr::memory_resource *m_resource;
        bool m_assume_padded;

    public:
        // 初始化并接收一段 JSON 文本
        explicit Parser(
            std::string_view json,
            std::pmr::memory_resource *res = Config::instance().resource(),
            bool assume_padded = false)
            : m_curr(json.data()),
              m_end(json.data() + json.size()),
              m_resource(res),
              m_assume_padded(assume_padded) {}

        // 主入口
        [[nodiscard]] Json parse();

    private:
        // 核心解析器函数
        void skip_whitespace();
        Json parse_value();
        void parse_value_inplace(Json &out);
        Json parse_object();
        Json parse_array();
        void parse_object_inplace(Json &out);
        void parse_array_inplace(Json &out);
        String parse_string();
        Json parse_number();
        Json parse_literal();

    public:
        [[nodiscard]] uint32_t parse_hex4();
    };
}

#endif // INCLUDE_PJH_JSON_PARSER_HPP
