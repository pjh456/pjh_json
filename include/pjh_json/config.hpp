#ifndef INCLUDE_PJH_JSON_CONFIG_HPP
#define INCLUDE_PJH_JSON_CONFIG_HPP

#include <atomic>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <shared_mutex>

namespace pjh::json
{
    /**
     * @brief Memory allocation policy for JSON values
     */
    enum class Storage
    {
        Pooled,       // std::pmr::unsynchronized_pool_resource (default)
        Arena,        // std::pmr::monotonic_buffer_resource
        SystemDefault // std::pmr::new_delete_resource
    };

    class Document;

    /**
     * @brief Global configuration singleton (storage policy, global resource)
     *
     * Threading contract:
     * - The knobs (strict_duplicate_keys / arena_block_size / max_depth /
     *   strip_bom / strict_utf8 / json5) and storage() are lock-free atomics: their
     *   setters/getters may be called concurrently from any thread; values are
     *   captured at operation start (strict_duplicate_keys is read at each
     *   object entry, i.e. from the next object onward).
     * - resource() may be called concurrently with itself and with any
     *   operation that uses the returned resource, as long as no
     *   release()/reset() overlaps.
     * - release()/reset() are EXCLUSIVE global teardown: they destroy and
     *   replace the global Document's arena, invalidating every pointer
     *   previously returned by resource(). They must not run concurrently with
     *   resource(), nor with any use of the global resource (including the
     *   default-argument resolution of Array/Object/Parser/clone/dump/own/
     *   insert and the lifetime of any value allocated through it). Callers
     *   must guarantee quiescence and destroy all globally-allocated values
     *   first. Holding a resource() pointer (or a value allocated from it)
     *   across release()/reset() is use-after-free.
     * The global Document provides a shared memory resource used as the
     * default allocator throughout the library.
     */
    class Config
    {
    public:
        /**
         * @brief Access the global singleton
         * @return Reference to the global Config instance
         */
        static Config &instance();

        /**
         * @brief Set storage policy and block size
         * @param storage   Allocation policy (default: Pooled)
         * @param block_size Block/chunk size for Pooled/Arena (default 4096)
         * @note Does NOT affect already-parsed Documents.
         */
        void configure(
            Storage storage = Storage::Pooled,
            size_t block_size = 4096);

        /**
         * @brief Get current arena resource (never null)
         * @return Pointer to the global memory_resource
         * @note Lock-free in the common case: the pointer is cached atomically
         *       and only invalidated by release()/reset(); the shared lock is
         *       taken solely to re-cache after such an invalidation.
         * @warning The returned pointer is invalidated by release()/reset(),
         *          which destroy the arena object it points to. Do not retain
         *          it across either call, and do not let release()/reset() run
         *          on another thread while the pointer or any value allocated
         *          through it is in use.
         */
        [[nodiscard]] std::pmr::memory_resource *resource() noexcept;
        /**
         * @brief Get current storage policy
         * @return Active Storage value
         */
        [[nodiscard]] Storage storage() const noexcept;

        /**
         * @brief Enable/disable duplicate key detection during parse
         * @note Lock-free: relaxed atomic store/load, safe to call
         *       concurrently with parse. The value is read at each object
         *       entry, so a change takes effect from the next object
         *       (capture-at-start granularity).
         * @note Default: false. When off, duplicate keys are allowed and
         *       resolve last-wins: the first occurrence keeps its key and
         *       position, its value is overwritten in place — the same
         *       semantics as Object::insert. When on, a duplicate key
         *       throws ParseError. JSON spec does not mandate rejection
         *       of duplicate keys.
         */
        void set_strict_duplicate_keys(bool enable) noexcept { m_strict_duplicate_keys.store(enable, std::memory_order_relaxed); }
        [[nodiscard]] bool strict_duplicate_keys() const noexcept { return m_strict_duplicate_keys.load(std::memory_order_relaxed); }

        /**
         * @brief Set arena initial block size for per-parse allocation (0 = auto)
         * @param bytes Arena buffer size in bytes. 0 (default) means
         *        auto-scale based on input size (input_size * 3, capped at
         *        16 GB; the cap is a saturating truncation applied before
         *        the multiply, so the estimate cannot overflow size_t).
         *        Non-zero uses the given fixed size for every parse.
         * @note Only meaningful with Storage::Arena.
         * @note Lock-free: relaxed atomic store/load, safe to call
         *       concurrently with parse. The value is captured at parse
         *       start; a change takes effect from the next parse
         *       (capture-at-start granularity).
         */
        void set_arena_block_size(size_t bytes) noexcept { m_arena_block_size.store(bytes, std::memory_order_relaxed); }
        [[nodiscard]] size_t arena_block_size() const noexcept { return m_arena_block_size.load(std::memory_order_relaxed); }

        /**
         * @brief Default nesting bound when set_max_depth() is never called
         * @note Root container counts as level 1; see set_max_depth().
         */
        static constexpr size_t kDefaultMaxDepth = 512;

        /**
         * @brief Sentinel for set_max_depth(): unlimited nesting (explicit opt-out)
         * @warning Unbounded recursion: only for fully trusted input. A deeply
         *          nested value can overflow the call stack (uncatchable crash).
         */
        static constexpr size_t kUnlimitedDepth = 0;

        /**
         * @brief Set maximum nesting depth for parse and dump
         * @param depth Maximum number of nested containers (objects/arrays);
         *        kUnlimitedDepth (0) means unlimited (explicit opt-out).
         *        Defaults to kDefaultMaxDepth (512); the root container
         *        counts as level 1.
         * @note Captured at parse start / dump start; a later call does not
         *       affect in-flight operations.
         * @note Lock-free: relaxed atomic store/load, safe to call
         *       concurrently with parse/dump.
         * @note A finite default is the DoS guard: a deeply nested input would
         *       otherwise overflow the call stack (an uncatchable crash). Keep
         *       the default when parsing untrusted input; pass kUnlimitedDepth
         *       explicitly and accept the stack risk only for trusted input.
         */
        void set_max_depth(size_t depth) noexcept { m_max_depth.store(depth, std::memory_order_relaxed); }
        [[nodiscard]] size_t max_depth() const noexcept { return m_max_depth.load(std::memory_order_relaxed); }

        /**
         * @brief Strip a leading UTF-8 BOM (EF BB BF) at parse entry
         * @note Lock-free: relaxed atomic store/load, safe to call
         *       concurrently with parse. The value is captured at each
         *       Parser construction (the single-value parse entries
         *       pass it); a change takes effect from the next entry
         *       (capture-at-start granularity).
         * @note Default: false. By default a leading BOM is rejected
         *       by the grammar ("Unexpected character parsing value"
         *       at offset 0). Only the UTF-8 BOM at byte 0 of the
         *       input is handled; UTF-16/32 BOMs are invalid UTF-8
         *       and remain rejected (no special-casing).
         * @note In parse_jsonl the whole-input start only counts: the
         *       BOM belongs to the file and is consumed once before
         *       the line scan; a BOM at the start of any later line is
         *       still a parse error at that line's offset 0.
         * @note The compile-time path (ConstJson::parse) cannot read
         *       runtime config (std::atomic is not constexpr): it
         *       stays BOM-strict regardless of this knob.
         * @note Orthogonal to strict_utf8: this knob rules only on the
         *       byte-0 prefix; strict_utf8 only sees bytes inside quotes
         *       (a BOM inside a string is U+FEFF data, legal UTF-8).
         */
        void set_strip_bom(bool enable) noexcept { m_strip_bom.store(enable, std::memory_order_relaxed); }
        [[nodiscard]] bool strip_bom() const noexcept { return m_strip_bom.load(std::memory_order_relaxed); }

        /**
         * @brief Enable strict UTF-8 validation of string content at parse
         * @note Lock-free: relaxed atomic store/load, safe to call
         *       concurrently with parse. The value is captured at each
         *       Parser construction (all five entries, including
         *       parse_jsonl's per-line parsers); a change takes effect
         *       from the next parse (capture-at-start granularity).
         * @note Default: false. By default string content is a byte
         *       mirror — ill-formed UTF-8 (0x80-0xC1 / 0xF5-0xFF
         *       leads, overlong encodings, surrogate range ED A0-BF,
         *       codepoints > U+10FFFF, truncated tails) is accepted and
         *       round-trips byte-identical.
         * @note Scope: bytes inside quotes only (values and object
         *       keys). The \\uXXXX escape path is already strict in
         *       every build (lone surrogates / bad pair ordering throw
         *       today); this knob adds the raw-byte rules only.
         * @note The writer is unaffected: non-ascii dump stays a byte
         *       mirror, ascii dump validates unconditionally
         *       (pre-existing, DumpOptions.ascii).
         * @note Errors report the first offending byte: offset relative
         *       to the original buffer start (parse_jsonl: to the line
         *       start). The compile-time path (ConstJson::parse) cannot
         *       read runtime config (std::atomic is not constexpr): it
         *       stays raw-byte-lenient by construction (documented
         *       divergence, pinned in tests/literal_test.cpp).
         * @note Orthogonal to strip_bom: a byte-0 BOM is a prefix
         *       policy (grammar), not a UTF-8 error; a BOM inside a
         *       string is U+FEFF data and passes this check.
         */
        void set_strict_utf8(bool enable) noexcept { m_strict_utf8.store(enable, std::memory_order_relaxed); }
        [[nodiscard]] bool strict_utf8() const noexcept { return m_strict_utf8.load(std::memory_order_relaxed); }

        /**
         * @brief Enable the JSON5 input superset at parse time
         * @note Lock-free: relaxed atomic store/load, safe to call
         *       concurrently with parse. The value is captured at each
         *       Parser construction; a change takes effect from the next
         *       parse (capture-at-start granularity).
         * @note Default: false = strict RFC 8259 (byte-for-byte the
         *       pre-JSON5 behavior). When true the parser additionally
         *       accepts line comments, slash-star block comments
         *       (non-nesting), one trailing comma per array/object,
         *       single-quoted strings, ASCII unquoted identifiers as
         *       object keys, the JSON5 ASCII whitespace bytes
         *       VT (0x0B) / FF (0x0C), the JSON5 number spellings
         *       (hex, leading `+`, leading/trailing `.`) and the
         *       `Infinity`/`NaN` literals with an optional `+`/`-`.
         * @note This is a PARTIAL JSON5 1.0.0 mode: Unicode identifiers
         *       and Unicode whitespace are still rejected; they land in
         *       a later sub-task. Unsupported JSON5 constructs fail with
         *       existing ErrorCodes (no JSON5-only error is introduced).
         * @note Output is always RFC 8259: dump() is unaffected by this
         *       knob, so a JSON5 parse followed by dump is a lossy
         *       normalization (comments dropped, single quotes and
         *       unquoted keys rewritten to double quotes, trailing
         *       commas removed, whitespace collapsed by the options).
         * @note A parsed non-finite double (`Infinity`/`NaN`) has no
         *       RFC 8259 spelling and cannot be dumped: dump() rejects it
         *       with a JsonError ("Cannot serialize non-finite double").
         *       That is a documented round-trip break inherent to a
         *       RFC-only writer, not a parse failure.
         * @note The compile-time path (ConstJson::parse) cannot read
         *       runtime config (std::atomic is not constexpr): it stays
         *       RFC-only regardless of this knob.
         * @note Orthogonal to strip_bom / strict_utf8: those keep their
         *       own semantics under either mode.
         */
        void set_json5(bool enable) noexcept { m_json5.store(enable, std::memory_order_relaxed); }
        [[nodiscard]] bool json5() const noexcept { return m_json5.load(std::memory_order_relaxed); }

        /**
         * @brief Release global document
         * @note In debug builds, asserts no outstanding allocations from the
         *       global resource.
         * @warning Exclusive global teardown: destroys and replaces the global
         *          arena. Not safe concurrently with resource() or with any use
         *          of the global resource; all values allocated from it must be
         *          destroyed and all other threads quiesced first. See the
         *          class-level threading contract.
         */
        void release();
        /**
         * @brief Reset config to defaults and release global document
         * @note Equivalent to configure(Pooled, 4096),
         *       strict_duplicate_keys = false, arena_block_size = 0 (auto),
         *       max depth = kDefaultMaxDepth (512, see set_max_depth()),
         *       strip_bom = false, strict_utf8 = false, json5 = false,
         *       then release().
         * @note Knob restoration uses atomic stores, still performed under
         *       the lock, in service of release_locked().
         * @warning Exclusive global teardown: destroys and replaces the global
         *          arena. Not safe concurrently with resource() or with any use
         *          of the global resource; all values allocated from it must be
         *          destroyed and all other threads quiesced first. See the
         *          class-level threading contract.
         */
        void reset();

    private:
        /**
         * @brief Private ctor -- singleton only
         */
        Config();
        /**
         * @brief Private dtor
         */
        ~Config();
        Config(const Config &) = delete;
        Config &operator=(const Config &) = delete;

        /**
         * @brief Release global document (must hold m_mutex)
         */
        void release_locked();

        std::atomic<bool> m_strict_duplicate_keys{false};
        std::atomic<size_t> m_arena_block_size{0};
        std::atomic<size_t> m_max_depth{kDefaultMaxDepth};
        std::atomic<bool> m_strip_bom{false};
        std::atomic<bool> m_strict_utf8{false};
        std::atomic<bool> m_json5{false};
        std::atomic<Storage> m_storage{Storage::Pooled};
        size_t m_block = 4096;
        std::unique_ptr<Document> m_global;
        // Fast-path cache of m_global->resource(): read lock-free in resource(),
        // invalidated by release()/reset() while holding m_mutex (reset()
        // rebuilds the arena, so the pointer changes).
        std::atomic<std::pmr::memory_resource *> m_cached_resource{nullptr};
        std::shared_mutex m_mutex;
    };
}

#endif
