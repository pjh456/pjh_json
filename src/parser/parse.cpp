#include "pjh_json/parser.hpp"
#include "pjh_json/document.hpp"
#include <fstream>
#include <cstring>

namespace pjh::json
{
    namespace
    {
        constexpr size_t kBlock = 4096;
        constexpr size_t kMinArenaBlock = 4096;
        constexpr size_t kMaxArenaBlock = size_t(1) << 34; // 16 GB cap

        /*
         * Determine arena initial block size for a given input length.
         *
         * 1. If user configured a fixed size via
         *    Config::set_arena_block_size(), use it directly.
         * 2. Otherwise auto-scale: use kBlock (4096) for small inputs,
         *    input_size * 3 for larger inputs (estimated DOM overhead),
         *    clamped to [kMinArenaBlock, kMaxArenaBlock].
         *
         * Auto-scaling avoids excessive intermediate buffer allocations
         * in monotonic_buffer_resource during vector growth.
         */
        static size_t arena_block_for(size_t input_len)
        {
            size_t cfg = Config::instance().arena_block_size();
            if (cfg > 0)
                return cfg;
            if (input_len <= kBlock)
                return kBlock;
            size_t est = input_len * 3;
            if (est < kMinArenaBlock)
                return kMinArenaBlock;
            if (est > kMaxArenaBlock)
                return kMaxArenaBlock;
            return est;
        }
        std::pmr::memory_resource *arena_res(
            const std::unique_ptr<std::pmr::memory_resource> &arena) noexcept
        {
            return arena ? arena.get() : std::pmr::new_delete_resource();
        }
    }

    /*
     * Parse a complete JSON value from the padded input.
     *
     * For inputs above Config::two_stage_threshold(), builds a SIMD
     * structural index and uses two-stage parsing. For smaller inputs,
     * falls back to recursive-descent (lower overhead, no index allocation).
     */
    Json Parser::parse()
    {
        if (!m_assume_padded)
            throw ParseError("Parser requires 64-byte '\\0' padding; use parse_copy/parse_file/parse_in_situ");

        size_t threshold = Config::instance().two_stage_threshold();
        if (threshold > 0 && m_data_len >= threshold)
        {
            Stage1Index idx = build_structural_index(m_data, m_data_len, m_resource);
            return parse_two_stage(idx);
        }

        Json result = parse_value();
        skip_whitespace();
        if (m_curr < m_end)
            throw ParseError("Extra characters after complete JSON value");
        return result;
    }

    /*
     * Parse a pre-padded buffer in-place (moves buffer ownership)
     *
     * 1. Validate buffer has >= 64 bytes (content + padding).
     * 2. Compute content size (total - 64).
     * 3. Create arena and parser, then parse.
     * 4. Return Document owning arena, tree, and buffer.
     *
     * The parsed tree borrows strings from the buffer, which stays alive
     * inside the Document.
     */
    Document parse_in_situ(std::pmr::string &&buffer, Storage storage)
    {
        if (buffer.size() < 64)
            throw ParseError("Buffer too small for in-situ parse");

        size_t size = buffer.size() - 64;
        size_t block = arena_block_for(size);
        auto arena = Document::make_arena(storage, block, false);
        Parser p(std::string_view(buffer.data(), size), arena_res(arena), true);
        Json root = p.parse();
        return Document(std::move(arena), std::move(root), std::move(buffer),
                        false, storage, block);
    }

    /*
     * Parse a copy of the input (input is padded internally)
     *
     * 1. Create arena.
     * 2. Allocate a buffer large enough for input + 64 NUL bytes.
     * 3. Copy input into the buffer.
     * 4. Parse from the padded buffer.
     * 5. Return Document owning arena, tree, and buffer copy.
     */
    Document parse_copy(std::string_view json, Storage storage)
    {
        size_t block = arena_block_for(json.size());
        auto arena = Document::make_arena(storage, block, false);
        std::pmr::memory_resource *res = arena_res(arena);

        std::pmr::string buffer(res);
        buffer.resize(json.size() + 64, '\0');
        std::memcpy(buffer.data(), json.data(), json.size());

        Parser p(std::string_view(buffer.data(), json.size()), res, true);
        Json root = p.parse();
        return Document(std::move(arena), std::move(root), std::move(buffer),
                        false, storage, block);
    }

    /*
     * Parse caller-owned memory (no copy)
     *
     * 1. Create arena (for parsed values only).
     * 2. Parse directly from caller's memory (no buffer copy).
     * 3. Return Document with is_view=true.
     *
     * Caller must keep data alive for the Document's lifetime.
     */
    Document parse_view(const char *data, size_t content_len, Storage storage)
    {
        size_t block = arena_block_for(content_len);
        auto arena = Document::make_arena(storage, block, false);
        Parser p(std::string_view(data, content_len), arena_res(arena), true);
        Json root = p.parse();
        return Document(std::move(arena), std::move(root), std::pmr::string{},
                        true, storage, block);
    }

    /*
     * Parse newline-delimited JSON (one value per line)
     *
     * 1. Create arena and padded buffer containing the entire input.
     * 2. Scan line by line:
     *    a. Find the next '\n'.
     *    b. Strip trailing '\r' (Windows line endings).
     *    c. Skip lines composed only of whitespace.
     *    d. Parse the line as a JSON value and append to an Array.
     * 3. Return Document containing the Array root.
     */
    Document parse_jsonl(std::string_view input, Storage storage)
    {
        size_t block = arena_block_for(input.size());
        auto arena = Document::make_arena(storage, block, false);
        std::pmr::memory_resource *res = arena_res(arena);

        // single padded buffer owned by Document; each line borrows into it
        std::pmr::string buffer(res);
        buffer.resize(input.size() + 64, '\0');
        std::memcpy(buffer.data(), input.data(), input.size());

        Array arr(res);
        const char *base = buffer.data();
        size_t i = 0;
        const size_t n = input.size();

        while (i < n)
        {
            // Find end of current line
            size_t nl = i;
            while (nl < n && base[nl] != '\n')
                ++nl;

            size_t len = nl - i;
            // Strip trailing \r for Windows line endings
            if (len > 0 && base[i + len - 1] == '\r')
                --len;

            // Skip blank/whitespace-only lines
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
                        false, storage, block);
    }

    /*
     * Parse a JSON file
     *
     * 1. Open file in binary mode, seek to end for total size.
     * 2. Allocate a buffer with 64-byte padding.
     * 3. Read the entire file into the buffer.
     * 4. Delegate to parse_in_situ for parsing.
     */
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
