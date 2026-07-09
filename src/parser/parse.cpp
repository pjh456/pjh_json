#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"
#include <fstream>
#include <cstring>

namespace pjh::json
{
    namespace
    {
        constexpr size_t kBlock = 4096;

        std::pmr::memory_resource *arena_res(
            const std::unique_ptr<std::pmr::memory_resource> &arena) noexcept
        {
            return arena ? arena.get() : std::pmr::new_delete_resource();
        }
    }

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

    Document parse_in_situ(std::pmr::string &&buffer, Storage storage)
    {
        if (buffer.size() < 64)
            throw ParseError("Buffer too small for in-situ parse");

        size_t size = buffer.size() - 64;
        auto arena = Document::make_arena(storage, kBlock, false);
        Parser p(std::string_view(buffer.data(), size), arena_res(arena), true);
        Json root = p.parse();
        return Document(std::move(arena), std::move(root), std::move(buffer),
                        false, storage, kBlock);
    }

    Document parse_copy(std::string_view json, Storage storage)
    {
        auto arena = Document::make_arena(storage, kBlock, false);
        std::pmr::memory_resource *res = arena_res(arena);

        std::pmr::string buffer(res);
        buffer.resize(json.size() + 64, '\0');
        std::memcpy(buffer.data(), json.data(), json.size());

        Parser p(std::string_view(buffer.data(), json.size()), res, true);
        Json root = p.parse();
        return Document(std::move(arena), std::move(root), std::move(buffer),
                        false, storage, kBlock);
    }

    Document parse_view(const char *data, size_t content_len, Storage storage)
    {
        auto arena = Document::make_arena(storage, kBlock, false);
        Parser p(std::string_view(data, content_len), arena_res(arena), true);
        Json root = p.parse();
        return Document(std::move(arena), std::move(root), std::pmr::string{},
                        true, storage, kBlock);
    }

    Document parse_jsonl(std::string_view input, Storage storage)
    {
        auto arena = Document::make_arena(storage, kBlock, false);
        std::pmr::memory_resource *res = arena_res(arena);

        // 单块 padded buffer 归 Document 所有，每行借用的字符串都指向它
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
            if (len > 0 && base[i + len - 1] == '\r')
                --len;

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

        return Document(std::move(arena), Json(std::move(arr)), std::move(buffer),
                        false, storage, kBlock);
    }

    Document parse_file(std::string_view filepath, Storage storage)
    {
        std::string path(filepath);
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
            throw ParseError("Failed to open file: " + path);

        std::streamsize size = file.tellg();
        if (size < 0)
            throw ParseError("Failed to get file size: " + path);
        file.seekg(0, std::ios::beg);

        std::pmr::string buffer;
        buffer.resize(size + 64, '\0');

        if (!file.read(buffer.data(), size))
            throw ParseError("Failed to read file: " + path);

        return parse_in_situ(std::move(buffer), storage);
    }
}
