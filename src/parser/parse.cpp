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

    Document parse_view(
        const char *data, size_t content_len,
        std::pmr::memory_resource *res)
    {
        Parser p(std::string_view(data, content_len), res, true);
        Json root = p.parse();
        return Document(std::move(root));
    }

    Document parse_jsonl(std::string_view input, std::pmr::memory_resource *res)
    {
        // One padded buffer owned by the Document; every line's borrowed
        // strings point into it, so nothing dangles.
        std::pmr::string buffer(res);
        buffer.resize(input.size() + 64, '\0');
        std::memcpy(buffer.data(), input.data(), input.size());

        Array arr(res);
        const char *base = buffer.data();
        size_t i = 0;
        const size_t n = input.size();

        while (i < n)
        {
            size_t nl = i;
            while (nl < n && base[nl] != '\n')
                ++nl;

            size_t len = nl - i;
            // trim trailing '\r' (CRLF)
            if (len > 0 && base[i + len - 1] == '\r')
                --len;

            // skip blank lines
            bool blank = true;
            for (size_t k = 0; k < len; ++k)
            {
                char c = base[i + k];
                if (c != ' ' && c != '\t')
                {
                    blank = false;
                    break;
                }
            }

            if (!blank)
            {
                Parser p(std::string_view(base + i, len), res, true);
                arr.push_back(p.parse());
            }

            i = (nl < n) ? nl + 1 : n;
        }

        return Document(Json(std::move(arr)), std::move(buffer));
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
