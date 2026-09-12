#include "pjh_json/parser.hpp"
#include "pjh_json/document.hpp"
#include <fstream>
#include <istream>
#include <cstring>

namespace pjh::json
{
    namespace
    {
        constexpr size_t kBlock = 4096;
        // Read block for the whole-stream buffering loop.
        constexpr size_t kStreamChunk = 8192;
        // Largest auto-scaled block: 16 GB on 64-bit size_t, 1 GB on 32-bit
        // where `size_t(1) << 34` would shift by the full type width (UB).
#if SIZE_MAX > 0xFFFFFFFFu
        constexpr size_t kMaxArenaBlock = size_t(1) << 34;
#else
        constexpr size_t kMaxArenaBlock = size_t(1) << 30;
#endif
        // The saturation test below divides the cap by 3: keep it small
        // enough that `input_len * 3` can never wrap size_t.
        static_assert(kMaxArenaBlock <= SIZE_MAX / 3,
                      "arena cap must leave the *3 estimate safe");

        /*
         * Determine arena initial block size for a given input length.
         *
         * 1. If user configured a fixed size via
         *    Config::set_arena_block_size(), use it directly.
         * 2. Otherwise auto-scale: use kBlock (4096) for small inputs,
         *    input_size * 3 for larger inputs (estimated DOM overhead),
         *    capped at kMaxArenaBlock. The cap is applied by saturating
         *    before the multiply, so the estimate cannot wrap size_t for
         *    pathological (especially 32-bit) input lengths.
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
            // Saturate before multiplying: above kMaxArenaBlock / 3 the
            // product is always over the cap, so return the cap directly.
            // A post-multiply clamp alone would let a wrapped product
            // re-enter the valid range and be returned as a "legitimate"
            // (but bogus) block size.
            if (input_len > kMaxArenaBlock / 3)
                return kMaxArenaBlock;
            return input_len * 3;
        }
        std::pmr::memory_resource *arena_res(
            const std::unique_ptr<std::pmr::memory_resource> &arena) noexcept
        {
            return arena ? arena.get() : std::pmr::new_delete_resource();
        }

        // n + kPaddingWidth must not wrap size_t (pathological ~16 EB inputs).
        void check_padded_fits(size_t n)
        {
            if (n > SIZE_MAX - kPaddingWidth)
                throw ParseError("Input too large to pad");
        }

        // The UTF-8 BOM — the only BOM this library recognizes. Checked
        // once per parse entry, at the input start only. Bytes compare
        // as uint8_t: 0xEF is a negative char on two's-complement hosts,
        // a signed `== 0xEF` test would never match.
        bool starts_with_utf8_bom(const char *p, size_t size)
        {
            if (size < 3)
                return false;
            const auto *b = reinterpret_cast<const uint8_t *>(p);
            return b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF;
        }
    }

    /*
     * Parse a complete JSON value from the padded input.
     *
     * 1. Reject if the caller did not promise NUL padding (flag only -
     *    parse() never inspects padding bytes).
     * 2. Parse the top-level value (skips leading whitespace).
     * 3. Skip trailing whitespace.
     * 4. Reject extra characters after the parsed value.
     */
    Json Parser::parse()
    {
        if (!m_assume_padded)
            throw ParseError(
                "Parser requires NUL padding (kPaddingWidth trailing"
                " NUL bytes); use the parse_* entry points");
        if (m_strip_bom)
            skip_leading_bom();
        Json result = parse_value();
        skip_whitespace();
        if (m_curr < m_end)
            throw_parse_error("Extra characters after complete JSON value", m_curr, m_begin);
        return result;
    }

    /*
     * BOM strip (task 23): at most once per parse, at the input start.
     * m_begin is deliberately untouched — error offsets stay relative
     * to the original buffer start (offset honesty).
     */
    void Parser::skip_leading_bom()
    {
        if (m_curr == m_begin &&
            starts_with_utf8_bom(m_curr, m_end - m_curr))
            m_curr += 3;
    }

    /*
     * Parse a pre-padded buffer in-place (moves buffer ownership)
     *
     * 1. Validate buffer has >= kPaddingWidth bytes (content + padding).
     * 2. Verify the trailing kPaddingWidth bytes are NUL (contract check);
     *    compute content size (total - kPaddingWidth).
     * 3. Create arena and parser, then parse.
     * 4. Return Document owning arena, tree, and buffer.
     *
     * The parsed tree borrows strings from the buffer, which stays alive
     * inside the Document.
     */
    Document parse_in_situ(std::pmr::string &&buffer, Storage storage)
    {
        if (buffer.size() < kPaddingWidth)
            throw ParseError("Buffer too small for in-situ parse");

        // The contract (document.hpp) requires the trailing kPaddingWidth
        // bytes to be NUL. They are inside the buffer, hence readable:
        // verify. A non-NUL tail means the caller broke the contract;
        // without this check the parser would silently read phantom
        // content.
        const char *pad = buffer.data() + (buffer.size() - kPaddingWidth);
        for (size_t i = 0; i < kPaddingWidth; ++i)
        {
            if (pad[i] != '\0')
                throw ParseError("In-situ buffer padding must be NUL bytes");
        }

        size_t size = buffer.size() - kPaddingWidth;
        size_t block = arena_block_for(size);
        auto arena = Document::make_arena(storage, block, false);
        Parser p(std::string_view(buffer.data(), size), arena_res(arena), true,
                Config::instance().strip_bom(), Config::instance().strict_utf8());
        Json root = p.parse();
        return Document(std::move(arena), std::move(root), std::move(buffer),
                        false, storage, block);
    }

    /*
     * Parse a copy of the input (input is padded internally)
     *
     * 1. Create arena.
     * 2. Allocate a buffer large enough for input + kPaddingWidth NUL bytes.
     * 3. Copy input into the buffer.
     * 4. Parse from the padded buffer.
     * 5. Return Document owning arena, tree, and buffer copy.
     */
    Document parse_copy(std::string_view json, Storage storage)
    {
        size_t block = arena_block_for(json.size());
        auto arena = Document::make_arena(storage, block, false);
        std::pmr::memory_resource *res = arena_res(arena);

        check_padded_fits(json.size());

        std::pmr::string buffer(res);
        buffer.resize(json.size() + kPaddingWidth, '\0');
        // memcpy(dst, null, 0) is UB (the source parameter is nonnull even
        // for n == 0): a default-constructed string_view has data()==nullptr.
        if (!json.empty())
            std::memcpy(buffer.data(), json.data(), json.size());

        Parser p(std::string_view(buffer.data(), json.size()), res, true,
                Config::instance().strip_bom(), Config::instance().strict_utf8());
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
        Parser p(std::string_view(data, content_len), arena_res(arena), true,
                Config::instance().strip_bom(), Config::instance().strict_utf8());
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
        check_padded_fits(input.size());

        std::pmr::string buffer(res);
        buffer.resize(input.size() + kPaddingWidth, '\0');
        // See parse_copy: guard the null source of an empty string_view.
        if (!input.empty())
            std::memcpy(buffer.data(), input.data(), input.size());

        Array arr(res);
        const char *base = buffer.data();
        size_t i = 0;
        const size_t n = input.size();

        // A BOM belongs to the file, not to line 1's value: consume it
        // once at the whole-input start. Later lines keep the strict
        // grammar — a BOM at their start is a parse error (per-line
        // parsers are constructed without the strip flag).
        if (Config::instance().strip_bom() && starts_with_utf8_bom(base, n))
            i = 3;

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

            // Skip blank/whitespace-only lines. The set must match
            // Parser::skip_whitespace (RFC 8259: space, tab, CR, LF;
            // LF cannot occur inside a line). A non-blank line therefore
            // always contains a byte the parser rejects in-line, so the
            // SIMD skip can never hop past this line into the next one.
            bool blank = true;
            for (size_t k = 0; k < len; ++k)
            {
                char c = base[i + k];
                if (c != ' ' && c != '\t' && c != '\r')
                {
                    blank = false;
                    break;
                }
            }

            if (!blank)
            {
                // Per-line parsers: the BOM flag stays false (the BOM
                // belongs to the whole input, consumed once above), but
                // the strict_utf8 gate applies to EVERY line — UTF-8
                // legality has no file-level vs line-level split.
                // Offsets come out line-relative automatically (m_begin
                // = line base).
                Parser p(std::string_view(base + i, len), res, true, false,
                        Config::instance().strict_utf8());
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
     * 2. Allocate a buffer with kPaddingWidth-byte padding.
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

        check_padded_fits(static_cast<size_t>(size));

        std::pmr::string buffer;
        buffer.resize(size + kPaddingWidth, '\0');

        if (!file.read(buffer.data(), size))
            throw ParseError("Failed to read file: " + path);

        return parse_in_situ(std::move(buffer), storage);
    }

    /*
     * Parse JSON from an input stream (whole-stream buffer, then delegate)
     *
     * 1. A stream already in bad state fails as a read error (context-free).
     * 2. Read the remaining content in kStreamChunk blocks. A 0-byte
     *    extraction is the normal end (empty stream, or clean EOF at a
     *    chunk boundary) and is NOT a read failure: implementations may set
     *    badbit on a 0-byte read, so the badbit test only applies to
     *    partial reads (hard I/O errors surface there, e.g. filebuf EIO).
     * 3. Pad with kPaddingWidth NUL bytes and delegate to parse_in_situ
     *    (same delegation parse_file uses at parse.cpp:322) — padding
     *    check, arena sizing, Parser construction and Document
     *    bookkeeping all happen there.
     * 4. Offsets are relative to the stream start (buffer start); read
     *    failures are context-free (offset 0), like parse_file's I/O
     *    failures; allocation failures propagate std::bad_alloc
     *    unconverted (plan 14 ruling).
     */
    Document parse_from_istream(std::istream &in, Storage storage)
    {
        if (in.bad())
            throw ParseError("Failed to read stream");

        std::pmr::string buffer;
        char chunk[kStreamChunk];
        for (;;)
        {
            in.read(chunk, static_cast<std::streamsize>(sizeof chunk));
            const std::streamsize got = in.gcount();
            if (got > 0)
                buffer.append(chunk, static_cast<size_t>(got));
            if (got == 0)
                break;              // 0-byte read: end of stream, not an error
            if (in.bad())
                throw ParseError("Failed to read stream");
            if (in.fail())
                break;              // clean end-of-stream mid-chunk
        }

        check_padded_fits(buffer.size());

        buffer.resize(buffer.size() + kPaddingWidth, '\0');

        return parse_in_situ(std::move(buffer), storage);
    }

    /*
     * Structured-entry shells (task 16): thin catch-and-wrap over the
     * throwing entries. Ladder: ParseError -> stored by value (offset and
     * category survive the copy — no slicing, E is pinned to the most
     * derived class the core can throw); JsonError base -> rethrown (not a
     * parse-core contract class today; keeping the cell is the safety rail
     * against a future core introducing one — never stored, never swallowed).
     * std::bad_alloc and other non-JsonError exceptions escape unconverted.
     */

    pjh::result::Result<Document, ParseError> parse_in_situ_result(std::pmr::string &&buffer, Storage storage)
    {
        try
        {
            return pjh::result::Result<Document, ParseError>::Ok(parse_in_situ(std::move(buffer), storage));
        }
        catch (const ParseError &e)
        {
            return pjh::result::Result<Document, ParseError>::Err(ParseError(e));
        }
        catch (const JsonError &)
        {
            throw;
        }
    }

    pjh::result::Result<Document, ParseError> parse_copy_result(std::string_view json, Storage storage)
    {
        try
        {
            return pjh::result::Result<Document, ParseError>::Ok(parse_copy(json, storage));
        }
        catch (const ParseError &e)
        {
            return pjh::result::Result<Document, ParseError>::Err(ParseError(e));
        }
        catch (const JsonError &)
        {
            throw;
        }
    }

    pjh::result::Result<Document, ParseError> parse_view_result(const char *data, size_t content_len, Storage storage)
    {
        try
        {
            return pjh::result::Result<Document, ParseError>::Ok(parse_view(data, content_len, storage));
        }
        catch (const ParseError &e)
        {
            return pjh::result::Result<Document, ParseError>::Err(ParseError(e));
        }
        catch (const JsonError &)
        {
            throw;
        }
    }

    pjh::result::Result<Document, ParseError> parse_jsonl_result(std::string_view input, Storage storage)
    {
        try
        {
            return pjh::result::Result<Document, ParseError>::Ok(parse_jsonl(input, storage));
        }
        catch (const ParseError &e)
        {
            return pjh::result::Result<Document, ParseError>::Err(ParseError(e));
        }
        catch (const JsonError &)
        {
            throw;
        }
    }

    pjh::result::Result<Document, ParseError> parse_file_result(std::string_view filepath, Storage storage)
    {
        try
        {
            return pjh::result::Result<Document, ParseError>::Ok(parse_file(filepath, storage));
        }
        catch (const ParseError &e)
        {
            return pjh::result::Result<Document, ParseError>::Err(ParseError(e));
        }
        catch (const JsonError &)
        {
            throw;
        }
    }

    pjh::result::Result<Document, ParseError>
    parse_from_istream_result(std::istream &in, Storage storage)
    {
        try
        {
            return pjh::result::Result<Document, ParseError>::Ok(
                parse_from_istream(in, storage));
        }
        catch (const ParseError &e)
        {
            return pjh::result::Result<Document, ParseError>::Err(ParseError(e));
        }
        catch (const JsonError &)
        {
            throw;
        }
    }
}
