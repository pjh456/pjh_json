#include "pjh_json/parser.hpp"
#include "pjh_json/document.hpp"
#include "pjh_json/detail/utils.hpp"
#include "pjh_json/grammar.hpp"
#include "unicode.hpp"
#include <fstream>
#include <istream>
#include <cstring>
#include <filesystem>
#include <system_error>

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

        // size_t overflow gate for n + kPaddingWidth (was check_padded_fits,
        // which threw; the impls record the kernel ErrorCode instead).
        [[nodiscard]] bool padded_fits(size_t n) noexcept
        {
            return n <= SIZE_MAX - kPaddingWidth;
        }

        // Materialise a kernel failure as the owned public exception WHILE any
        // borrowed detail is still alive. Called from the *_impl bodies only
        // (never from the public shells): a duplicate-key detail points into a
        // local parse buffer that dies at the impl's return, so the string
        // copy must happen before that.
        [[nodiscard]] pjh::result::Result<Document, ParseError>
        parse_error(const Error &e)
        {
            return pjh::result::Result<Document, ParseError>::Err(ParseError(e));
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
     * Zero-throw kernel: parse a complete JSON value from the padded input.
     *
     * 1. Reject if the caller did not promise NUL padding (flag only -
     *    parse() never inspects padding bytes).
     * 2. Parse the top-level value (skips leading whitespace).
     * 3. Skip trailing whitespace.
     * 4. Reject extra characters after the parsed value.
     *
     * The first failure is recorded in m_error; every later failure is
     * ignored (first error wins), mirroring the old immediate stack unwind.
     */
    bool Parser::parse(Json &out)
    {
        if (!m_assume_padded)
        {
            fail_context(ErrorCode::ParserRequiresPadding);
            return false;
        }
        if (m_strip_bom)
            skip_leading_bom();
        if (!parse_value(out))
            return false;
        // A JSON5 trivia skip can record a failure (unterminated block
        // comment) without propagating a false return up the structural
        // recursion (skip_whitespace stays void). Convert that recorded
        // first error into a kernel failure here, at the single top level.
        if (m_has_error)
            return false;
        skip_whitespace();
        if (m_has_error)
            return false;
        if (m_curr < m_end)
        {
            fail(ErrorCode::ExtraCharactersAfterValue, m_curr);
            return false;
        }
        return true;
    }

    /*
     * Compatibility shell: keeps the original signature and behavior. The
     * kernel records the first failure; materialise it as a ParseError in
     * this frame (detail is copied into the owned what() string).
     */
    Json Parser::parse()
    {
        Json out;
        if (!parse(out))
            throw ParseError(m_error);
        return out;
    }

    /*
     * BOM strip: at most once per parse, at the input start.
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
    static pjh::result::Result<Document, ParseError>
    parse_in_situ_impl(std::pmr::string &&buffer, Storage storage)
    {
        if (buffer.size() < kPaddingWidth)
            return parse_error(Error{ErrorCode::BufferTooSmall,
                                     Category::Parse, 0, false, {}});

        // The contract (document.hpp) requires the trailing kPaddingWidth
        // bytes to be NUL. They are inside the buffer, hence readable:
        // verify. A non-NUL tail means the caller broke the contract;
        // without this check the parser would silently read phantom
        // content.
        const char *pad = buffer.data() + (buffer.size() - kPaddingWidth);
        for (size_t i = 0; i < kPaddingWidth; ++i)
        {
            if (pad[i] != '\0')
                return parse_error(Error{ErrorCode::InSituPaddingNotNul,
                                         Category::Parse, 0, false, {}});
        }

        size_t size = buffer.size() - kPaddingWidth;
        size_t block = arena_block_for(size);
        auto arena = Document::make_arena(storage, block, false);
        Parser p(std::string_view(buffer.data(), size), arena_res(arena), true,
                Config::instance().strip_bom(), Config::instance().strict_utf8());
        Json root;
        if (!p.parse(root))
            return parse_error(p.error()); // buffer alive -> borrowed detail safe

        return pjh::result::Result<Document, ParseError>::Ok(
            Document(std::move(arena), std::move(root), std::move(buffer),
                     false, storage, block));
    }

    /*
     * Compatibility shell: the Result form is the primitive; this entry
     * materialises the owned ParseError (already done inside the impl) and
     * throws it, or returns the Document.
     */
    Document parse_in_situ(std::pmr::string &&buffer, Storage storage)
    {
        auto r = parse_in_situ_impl(std::move(buffer), storage);
        if (r.is_err())
            throw std::move(r).unwrap_err();
        return std::move(r).unwrap();
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
    static pjh::result::Result<Document, ParseError>
    parse_copy_impl(std::string_view json, Storage storage)
    {
        size_t block = arena_block_for(json.size());
        auto arena = Document::make_arena(storage, block, false);
        std::pmr::memory_resource *res = arena_res(arena);

        if (!padded_fits(json.size()))
            return parse_error(Error{ErrorCode::InputTooLarge,
                                     Category::Parse, 0, false, {}});

        std::pmr::string buffer(res);
        buffer.resize(json.size() + kPaddingWidth, '\0');
        // memcpy(dst, null, 0) is UB (the source parameter is nonnull even
        // for n == 0): a default-constructed string_view has data()==nullptr.
        if (!json.empty())
            std::memcpy(buffer.data(), json.data(), json.size());

        Parser p(std::string_view(buffer.data(), json.size()), res, true,
                Config::instance().strip_bom(), Config::instance().strict_utf8());
        Json root;
        if (!p.parse(root))
            return parse_error(p.error());

        return pjh::result::Result<Document, ParseError>::Ok(
            Document(std::move(arena), std::move(root), std::move(buffer),
                     false, storage, block));
    }

    Document parse_copy(std::string_view json, Storage storage)
    {
        auto r = parse_copy_impl(json, storage);
        if (r.is_err())
            throw std::move(r).unwrap_err();
        return std::move(r).unwrap();
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
    static pjh::result::Result<Document, ParseError>
    parse_view_impl(const char *data, size_t content_len, Storage storage)
    {
        size_t block = arena_block_for(content_len);
        auto arena = Document::make_arena(storage, block, false);
        Parser p(std::string_view(data, content_len), arena_res(arena), true,
                Config::instance().strip_bom(), Config::instance().strict_utf8());
        Json root;
        if (!p.parse(root))
            return parse_error(p.error());

        return pjh::result::Result<Document, ParseError>::Ok(
            Document(std::move(arena), std::move(root), std::pmr::string{},
                     true, storage, block));
    }

    Document parse_view(const char *data, size_t content_len, Storage storage)
    {
        auto r = parse_view_impl(data, content_len, storage);
        if (r.is_err())
            throw std::move(r).unwrap_err();
        return std::move(r).unwrap();
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
    static pjh::result::Result<Document, ParseError>
    parse_jsonl_impl(std::string_view input, Storage storage)
    {
        size_t block = arena_block_for(input.size());
        auto arena = Document::make_arena(storage, block, false);
        std::pmr::memory_resource *res = arena_res(arena);

        // single padded buffer owned by Document; each line borrows into it
        if (!padded_fits(input.size()))
            return parse_error(Error{ErrorCode::InputTooLarge,
                                     Category::Parse, 0, false, {}});

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

        // JSON5 mode widens the blank-line class to the JSON5 ASCII
        // whitespace set: under JSON5 a line of only VT/FF must be skipped
        // (otherwise a per-line Parser would be handed a blank line whose
        // trivia skip could reach into the next line). Read once; the
        // per-line Parsers capture the same value from Config.
        const bool json5 = Config::instance().json5();

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

            // Skip blank/whitespace-only lines. RFC mode delegates to
            // grammar::is_whitespace (space, tab, CR, LF; LF cannot occur
            // inside a line, so including it is a no-op here). JSON5 mode
            // also accepts VT/FF and the multi-byte JSON5 white space
            // (NBSP/LS/PS/U+FEFF/Zs), decoded strictly and line-bounded so a
            // malformed sequence leaves the line non-blank. A non-blank line
            // therefore always contains a byte the parser rejects in-line, so
            // the JSON5 trivia scan can never hop past this line into the
            // next one (it is additionally m_end-bounded).
            bool blank = true;
            if (json5)
            {
                size_t k = 0;
                while (k < len)
                {
                    const auto c = static_cast<unsigned char>(base[i + k]);
                    if (c < 0x80)
                    {
                        if (!grammar::is_json5_whitespace_ascii(c))
                        {
                            blank = false;
                            break;
                        }
                        ++k;
                        continue;
                    }
                    uint32_t cp = 0;
                    size_t unit = 0;
                    if (!unicode::decode_utf8(base + i + k, base + i + len, cp, unit) ||
                        !grammar::is_json5_whitespace(cp))
                    {
                        blank = false;
                        break;
                    }
                    k += unit;
                }
            }
            else
            {
                for (size_t k = 0; k < len; ++k)
                {
                    if (!grammar::is_whitespace(static_cast<unsigned char>(base[i + k])))
                    {
                        blank = false;
                        break;
                    }
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
                Json v;
                if (!p.parse(v))
                    return parse_error(p.error());
                arr.push_back(std::move(v));
            }

            i = (nl < n) ? nl + 1 : n;
        }

        return pjh::result::Result<Document, ParseError>::Ok(
            Document(std::move(arena), Json(std::move(arr)), std::move(buffer),
                     false, storage, block));
    }

    Document parse_jsonl(std::string_view input, Storage storage)
    {
        auto r = parse_jsonl_impl(input, storage);
        if (r.is_err())
            throw std::move(r).unwrap_err();
        return std::move(r).unwrap();
    }

    /*
     * Parse a JSON file
     *
     * 1. Open file in binary mode, seek to end for total size.
     * 2. Allocate a buffer with kPaddingWidth-byte padding.
     * 3. Read the entire file into the buffer.
     * 4. Delegate to parse_in_situ for parsing.
     */
    static pjh::result::Result<Document, ParseError>
    parse_file_impl(std::string_view filepath, Storage storage)
    {
        std::string path(filepath);

        // A directory is not a readable JSON file. On POSIX some filesystem /
        // libc combinations let std::ifstream open a directory anyway; tellg()
        // then reports an unusable value (INT64_MAX on ext4/overlayfs) that
        // passes the signed and padded_fits guards, after which resize() throws
        // std::length_error. Reject the path up front as a context-free open
        // failure: this both matches the archived golden behavior and makes the
        // classification filesystem-independent (tmpfs already fails is_open()).
        // is_directory uses the error_code overload: it never throws.
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec))
            return parse_error(Error{ErrorCode::FileOpenFailed,
                                     Category::Parse, 0, false, filepath});

        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
            return parse_error(Error{ErrorCode::FileOpenFailed,
                                     Category::Parse, 0, false, filepath});

        std::streamsize size = file.tellg();
        if (size < 0)
            return parse_error(Error{ErrorCode::FileSizeFailed,
                                     Category::Parse, 0, false, filepath});
        file.seekg(0, std::ios::beg);

        if (!padded_fits(static_cast<size_t>(size)))
            return parse_error(Error{ErrorCode::InputTooLarge,
                                     Category::Parse, 0, false, {}});

        std::pmr::string buffer; // default-resource buffer
        // tellg() can report a value that passes padded_fits yet exceeds the
        // buffer's representable size (e.g. INT64_MAX); resize() would then
        // throw std::length_error instead of a typed ParseError. Reject anything
        // the buffer cannot hold before allocating. This is exactly resize()'s
        // precondition, so it is portable across standard libraries.
        if (buffer.max_size() < kPaddingWidth ||
            static_cast<size_t>(size) > buffer.max_size() - kPaddingWidth)
            return parse_error(Error{ErrorCode::FileSizeFailed,
                                     Category::Parse, 0, false, filepath});

        buffer.resize(size + kPaddingWidth, '\0');

        if (!file.read(buffer.data(), size))
            return parse_error(Error{ErrorCode::FileReadFailed,
                                     Category::Parse, 0, false, filepath});

        return parse_in_situ_impl(std::move(buffer), storage);
    }

    Document parse_file(std::string_view filepath, Storage storage)
    {
        auto r = parse_file_impl(filepath, storage);
        if (r.is_err())
            throw std::move(r).unwrap_err();
        return std::move(r).unwrap();
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
     *    unconverted.
     */
    static pjh::result::Result<Document, ParseError>
    parse_from_istream_impl(std::istream &in, Storage storage)
    {
        if (in.bad())
            return parse_error(Error{ErrorCode::StreamReadFailed,
                                     Category::Parse, 0, false, {}});

        std::pmr::string buffer;
        char chunk[kStreamChunk];
        for (;;)
        {
            in.read(chunk, static_cast<std::streamsize>(sizeof chunk));
            const std::streamsize got = in.gcount();
            if (got > 0)
                buffer.append(chunk, static_cast<size_t>(got));
            if (got == 0)
                break; // 0-byte read: end of stream, not an error
            if (in.bad())
                return parse_error(Error{ErrorCode::StreamReadFailed,
                                         Category::Parse, 0, false, {}});
            if (in.fail())
                break; // clean end-of-stream mid-chunk
        }

        if (!padded_fits(buffer.size()))
            return parse_error(Error{ErrorCode::InputTooLarge,
                                     Category::Parse, 0, false, {}});

        buffer.resize(buffer.size() + kPaddingWidth, '\0');
        return parse_in_situ_impl(std::move(buffer), storage);
    }

    Document parse_from_istream(std::istream &in, Storage storage)
    {
        auto r = parse_from_istream_impl(in, storage);
        if (r.is_err())
            throw std::move(r).unwrap_err();
        return std::move(r).unwrap();
    }

    /*
     * Short-name forward to parse_from_istream: the stream entry mirrors the
     * write-side dump_to short name. One line, zero new behavior — all
     * buffering, offsets, error classification and Config inheritance belong
     * to parse_from_istream.
     */
    Document parse(std::istream &in, Storage storage)
    {
        return parse_from_istream(in, storage);
    }

    /*
     * *_result entries are the primitive form: each forwards straight to its
     * zero-throw *_impl, which materialises the owned ParseError in its own
     * frame (while any borrowed detail is still alive). No catch anywhere;
     * std::bad_alloc and other non-JsonError exceptions escape unconverted.
     */

    pjh::result::Result<Document, ParseError> parse_in_situ_result(std::pmr::string &&buffer, Storage storage)
    {
        return parse_in_situ_impl(std::move(buffer), storage);
    }

    pjh::result::Result<Document, ParseError> parse_copy_result(std::string_view json, Storage storage)
    {
        return parse_copy_impl(json, storage);
    }

    pjh::result::Result<Document, ParseError> parse_view_result(const char *data, size_t content_len, Storage storage)
    {
        return parse_view_impl(data, content_len, storage);
    }

    pjh::result::Result<Document, ParseError> parse_jsonl_result(std::string_view input, Storage storage)
    {
        return parse_jsonl_impl(input, storage);
    }

    pjh::result::Result<Document, ParseError> parse_file_result(std::string_view filepath, Storage storage)
    {
        return parse_file_impl(filepath, storage);
    }

    pjh::result::Result<Document, ParseError>
    parse_from_istream_result(std::istream &in, Storage storage)
    {
        return parse_from_istream_impl(in, storage);
    }
}
