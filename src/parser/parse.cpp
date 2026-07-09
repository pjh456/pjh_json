#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"
#include <fstream>
#include <cstring>

namespace pjh::json
{
    Json Parser::parse()
    {
        if (!m_assume_padded)
            throw ParseError("Parser requires 64-byte '\\0' padding; use parse_copy/parse_file/parse_in_situ");
        Json result = parse_value();
        skip_whitespace();
        if (m_curr < m_end)
            throw ParseError("Extra characters after complete JSON value");
        return result;
    }

    Document parse_in_situ(
        std::pmr::string &&buffer,
        std::pmr::memory_resource *res)
    {
        if (buffer.size() < 64)
            throw ParseError("Buffer too small for in-situ parse");

        size_t size = buffer.size() - 64;
        Parser p(std::string_view(buffer.data(), size), res, true);
        Json root = p.parse();
        return Document(std::move(root), std::move(buffer));
    }

    Document parse_copy(
        std::string_view json,
        std::pmr::memory_resource *res)
    {
        std::pmr::string buffer(res);
        buffer.resize(json.size() + 64, '\0');
        std::memcpy(buffer.data(), json.data(), json.size());
        Parser p(std::string_view(buffer.data(), json.size()), res, true);
        Json root = p.parse();
        return Document(std::move(root), std::move(buffer));
    }

    Document parse_file(
        std::string_view filepath,
        std::pmr::memory_resource *res)
    {
        std::string path(filepath);
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
            throw ParseError("Failed to open file: " + path);

        std::streamsize size = file.tellg();
        if (size < 0)
            throw ParseError("Failed to get file size: " + path);
        file.seekg(0, std::ios::beg);

        std::pmr::string buffer(res);
        buffer.resize(size + 64, '\0');

        if (!file.read(buffer.data(), size))
            throw ParseError("Failed to read file: " + path);

        return parse_in_situ(std::move(buffer), res);
    }
}
