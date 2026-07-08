#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"
#include <fstream>
#include <cstring>

namespace pjh::json
{
    Json Parser::parse()
    {
        if (!m_assume_padded)
            error("Parser requires 64-byte '\\0' padding; use parse_copy/parse_file/parse_in_situ");
        Json result = parse_value();
        skip_whitespace();
        if (m_curr < m_end)
            error("Extra characters after complete JSON value");
        return result;
    }

    Document parse_in_situ(
        std::pmr::string &&buffer,
        std::pmr::memory_resource *res)
    {
        if (buffer.size() < 64)
            throw std::runtime_error("Buffer too small for in-situ parse");

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
        const std::string &filepath,
        std::pmr::memory_resource *res)
    {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open())
            throw std::runtime_error("Failed to open file: " + filepath);

        std::streamsize size = file.tellg();
        if (size < 0)
            throw std::runtime_error("Failed to get file size: " + filepath);
        file.seekg(0, std::ios::beg);

        std::pmr::string buffer(res);
        buffer.resize(size + 64, '\0');

        if (!file.read(buffer.data(), size))
            throw std::runtime_error("Failed to read file: " + filepath);

        return parse_in_situ(std::move(buffer), res);
    }
}
