#ifndef PJH_JSON_PARSER_INTERNAL_HPP
#define PJH_JSON_PARSER_INTERNAL_HPP

#include <cstdint>

namespace pjh::json
{
    class Parser;

    void encode_utf8(uint32_t cp, char *&dst);
    void handle_escape(char *&dst, const char *&m_curr, Parser &parser);
}

#endif
