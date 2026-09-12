#ifndef INCLUDE_PJH_JSON_OBJECT_HPP
#define INCLUDE_PJH_JSON_OBJECT_HPP

#include <stdexcept>
#include <string>
#include <string_view>
#include <memory>
#include <ranges>
#include <memory_resource>
#include <vector>
#include <concepts>
#include <utility>

#include "config.hpp"

#include "json_fwd.hpp"

namespace pjh::json
{
    class Json;

    // Key/value projection views (defined in json.hpp, after class Json —
    // the value iterators need the complete Json type).
    class KeysView;
    class ValuesView;
    class ConstValuesView;

    /**
     * @brief JSON object type (ordered map of string -> Json)
     *
     * Backed by std::pmr::vector<Entry> for insertion-order preservation.
     * Copy disabled -- use clone().
     *
     * @note Key lifetime: keys inserted via insert(string_view, Json) or
     *       operator[] are borrowed views, valid only while their source
     *       memory is alive (the owning Document's buffer, or the caller's
     *       data for parse_view); a destroyed, moved or reset() source
     *       leaves them dangling. To make a key independent of its source,
     *       use insert(key, val, res) (owned, copied into res) or clone().
     * @note The key is read-only through the iterators; data() remains the
     *       raw advanced surface (its key side is still writable by design).
     */
    class Object
    {
    public:
        using Entry = std::pair<String, Json>;
        using Vec = std::pmr::vector<Entry>;

        /**
         * @brief Const-track iterator (std::vector const_iterator; the
         *        key side is already read-only through the const pair)
         */
        using const_iterator = Vec::const_iterator;

        /**
         * @brief One iteration step of the non-const track
         *
         * Aggregate of two references into the stored entry. The key
         * side is const: a key is lookup identity — replacing one would
         * silently re-key the entry (find miss on the old key, a
         * duplicate-key state that insert/parse never produce) and can
         * downgrade an owned key to a view of dying memory. The value
         * side stays patchable.
         */
        struct EntryRef
        {
            const String &first; ///< read-only key (was mutable pre-21.1)
            Json &second;         ///< the value, in place
        };

        /**
         * @brief Non-const iterator over (String, Json) entries
         *
         * operator* yields a fresh EntryRef per call (key read-only,
         * value patchable); operator-> re-binds the cached EntryRef on
         * every call (a pointer held across ++ reads at most a stale
         * value, never a dangling one — the cache lives in the
         * iterator). Pre-increment only: the iterator is non-copyable
         * (the cache holds reference members). No iterator_traits
         * typedefs: range-for is the only promised scenario.
         * Dereferencing end() is undefined, as with every std container
         * iterator.
         * @warning Do not hold the address of a loop variable (&e)
         * across ++: the yield is a per-step proxy, not the stored
         * entry (the pre-21.1 raw pair& yield made that aliasing
         * sound; the writer's sort path uses data() instead).
         */
        class iterator
        {
        public:
            /** @brief The entry at the cursor (prvalue proxy into the container) */
            [[nodiscard]] EntryRef operator*() const noexcept;

            /** @brief Arrow access (re-bound on every call, see class doc) */
            [[nodiscard]] const EntryRef *operator->() const noexcept;

            /** @brief Advance one entry (pre-increment only, undefined past end) */
            iterator &operator++() noexcept;

            /**
             * @brief Copy not allowed — the cached proxy holds reference
             *        members (move-only by construction)
             */
            iterator(const iterator &) = delete;
            /**
             * @brief Move construct (reference members rebind to the same
             *        referents; the moved-from cache stays a legal, live
             *        binding — operator* never reads the cache and
             *        operator->/operator++ re-bind before any read)
             * @param other Source iterator (left usable-but-stale)
             */
            iterator(iterator &&) noexcept = default;
            /**
             * @brief Copy not allowed — the cached proxy holds reference
             *        members (move-only by construction)
             */
            iterator &operator=(const iterator &) = delete;

            friend bool operator==(const iterator &a, const iterator &b) noexcept;
            friend bool operator!=(const iterator &a, const iterator &b) noexcept;

        private:
            friend class Object;
            explicit iterator(Vec::iterator it) noexcept;
            Vec::iterator m_it;
            mutable EntryRef m_ref;
        };

    private:
        /**
         * @brief Flat open-addressing key -> entry-position index
         *
         * Cache only: an absent (or dropped) index means lookups fall back
         * to the linear scan, so a stale index can only be slow, never
         * wrong. Only materialised above kIndexThreshold by the write
         * paths; below it the linear sweep of a handful of short keys
         * beats hashing. Defined out of line (incomplete here).
         */
        struct Index;

        /**
         * @brief Entry count above which the write paths materialise the index
         *
         * Kept low enough that the public bulk-construction APIs turn O(N^2)
         * into O(N), high enough that the small-object hot path (benchmark
         * objects average ~3 keys, standalone objects 1-5) never pays for a
         * hash table.
         */
        static constexpr size_t kIndexThreshold = 16;

        /// Miss sentinel returned by find_slot (a valid position is < size())
        static constexpr size_t npos = static_cast<size_t>(-1);

        Vec m_data;
        std::pmr::memory_resource *m_resource{nullptr};
        Index *m_index{nullptr};

        /// First entry position whose key equals `key`, or npos. Uses the
        /// index when present, else the linear scan; never allocates. With
        /// `rebuild` false a stale index is bypassed (linear) instead of
        /// being refilled — used by remove, where refilling per erase would
        /// not pay off.
        [[nodiscard]] size_t find_slot(std::string_view key,
                                       bool rebuild = true) const noexcept;
        /// (Re)build the index over every current entry (first-wins). Drops
        /// any existing index first, so a throw leaves a valid null cache.
        void index_build();
        /// Refill an existing slot array from m_data (first-wins), clearing
        /// the stale flag; never allocates.
        void index_refill(Index &idx) const noexcept;
        /// Maintain the index after `pos` was appended (build/refill/grow/no-op).
        void index_note_append(size_t pos);
        /// Mark the index stale after an erase (or drop it below threshold);
        /// O(1), no slot-array walk.
        void index_after_erase() noexcept;
        /// Store `pos` under its key; skips a key already present.
        void index_insert(Index &idx, size_t pos) const noexcept;
        /// Release the index (no-op when absent); safe to call any time.
        void index_free() noexcept;

        friend class Json;

    public:
        /**
         * @brief Construct empty object with given allocator
         * @param res Memory resource for entries (default: global config resource)
         */
        Object(
            std::pmr::memory_resource *res = Config::instance().resource());

        /**
         * @brief Construct from existing vector (adopts allocator)
         * @param val Pre-populated vector; allocator is inferred from val
         */
        Object(Vec val);

        ~Object();

        /**
         * @brief Copy not allowed -- use clone()
         */
        Object(const Object &) = delete;
        /**
         * @brief Copy not allowed -- use clone()
         */
        Object &operator=(const Object &) = delete;

        /**
         * @brief Move construct
         * @param other Source object (left empty)
         * @note The source keeps its resource after the move, so a
         *       moved-from object stays adoptable (Json node allocation)
         *       into that resource.
         */
        Object(Object &&) noexcept;
        /**
         * @brief Move assign
         * @param other Source object (left empty)
         * @return *this
         * @note Cross-resource move-assign moves the entries into this
         *       object's own resource; this object keeps its own resource
         *       and the source keeps an empty buffer in its own, so the
         *       source's resource must outlive the source. On the
         *       same-resource path the moved-from source stays bound to the
         *       shared resource and remains adoptable (Json node).
         */
        Object &operator=(Object &&) noexcept;

        /**
         * @brief Deep copy into specified memory resource
         * @param into Allocator for copied entries (default: global config
         *        resource; nullptr also falls back to it)
         * @return Independent deep copy
         */
        [[nodiscard]] Object clone(
            std::pmr::memory_resource *into = Config::instance().resource()) const;

        /**
         * @brief Construct object from variadic entries
         * @tparam Es Types convertible to Object::Entry
         * @param entries Key-value pairs to insert
         * @return Object containing all entries
         */
        template <class... Es>
            requires(std::convertible_to<Es, Object::Entry> && ...)
        [[nodiscard]] static Object of(Es &&...entries);

    public:
        /**
         * @brief Entry count
         * @return Number of key-value pairs
         */
        [[nodiscard]] size_t size() const noexcept;

        /**
         * @brief true if no entries
         */
        [[nodiscard]] bool empty() const noexcept;

        /**
         * @brief Remove all entries
         */
        void clear() noexcept;

        /**
         * @brief true if key exists
         * @param key Field name to search
         * @return true if key found
         */
        [[nodiscard]] bool contains(std::string_view key) const noexcept;

        /**
         * @brief Iterator to first entry
         * @return Iterator over (String, Json) entries — the key side
         *         is read-only (a mutable key would silently re-key the
         *         entry or dangle its source; task 21.1), the value
         *         side stays patchable
         */
        [[nodiscard]] iterator begin() noexcept;
        /**
         * @brief Iterator past last entry
         * @return Iterator
         */
        [[nodiscard]] iterator end() noexcept;
        /**
         * @brief Const iterator to first entry
         * @return Const iterator
         */
        [[nodiscard]] Vec::const_iterator begin() const noexcept;
        /**
         * @brief Const iterator past last entry
         * @return Const iterator
         */
        [[nodiscard]] Vec::const_iterator end() const noexcept;

        /**
         * @brief Direct access to underlying vector
         * @return Mutable reference to internal Vec
         * @note The mutable surface can re-key, reorder or drop entries
         *       behind the class's back, so it drops the lookup index (a
         *       cache): subsequent lookups fall back to the linear scan
         *       until a write path rebuilds it. The vector itself is
         *       untouched.
         */
        [[nodiscard]] Vec &data() noexcept
        {
            if (m_index)
                index_free();
            return m_data;
        }
        /**
         * @brief Direct access to underlying vector (const)
         * @return Const reference to internal Vec
         */
        [[nodiscard]] const Vec &data() const noexcept { return m_data; }

    public:
        /** @name Field access */
        /**@{*/
        /**
         * @brief Access or insert key
         * @param key Field name
         * @return Mutable reference to Json for key
         * @note If key does not exist, default-constructed Json is inserted.
         * @note The key is borrowed on insert; for keys that must outlive
         *       their source use insert(key, val, res).
         */
        Json &operator[](std::string_view key);
        /**
         * @brief Access key (read-only)
         * @param key Field name
         * @return Const reference to Json for key
         * @throws std::out_of_range if key not found
         */
        const Json &operator[](std::string_view key) const;

        /**
         * @brief Access key with bounds check
         * @param key Field name
         * @return Mutable reference to Json for key
         * @throws std::out_of_range if key not found
         */
        Json &at(std::string_view key);
        /**
         * @brief Const access key with bounds check
         * @param key Field name
         * @return Const reference to Json for key
         * @throws std::out_of_range if key not found
         */
        const Json &at(std::string_view key) const;
        /**@}*/

    public:
        /** @name Key/value projections */
        /**@{*/
        /**
         * @brief The object's keys, in entry (insertion) order
         * @return Zero-allocation view (a single pointer into the entry
         *         vector)
         * @note Read-only keys: the key is lookup identity and has no
         *       mutation path; for mutable values use values().
         * @note A duplicate-key overwrite keeps the first occurrence's
         *       position (last-wins semantics, task 10).
         * @code
         * for (std::string_view k : obj.keys())
         *     log(k);
         * @endcode
         */
        [[nodiscard]] KeysView keys() const noexcept;
        /**
         * @brief The object's values (mutable), in entry order
         * @return Zero-allocation view; the loop variable is a Json &
         */
        [[nodiscard]] ValuesView values() noexcept;
        /**
         * @brief The object's values (read-only), in entry order
         * @return Zero-allocation view; the loop variable is a const Json &
         */
        [[nodiscard]] ConstValuesView values() const noexcept;
        /**@}*/

    public:
        /**
         * @brief Insert or overwrite key-value pair
         * @param key Field name
         * @param val Value to assign
         * @note The key is borrowed: it must stay valid for the Object's
         *       lifetime. Do not pass a temporary std::string or a document
         *       buffer that may die before the Object; use
         *       insert(key, val, res) to own the key.
         * @note The parser applies the same last-wins rule for duplicate
         *       keys unless strict_duplicate_keys is on
         *       (Config::set_strict_duplicate_keys).
         */
        void insert(std::string_view key, Json val);
        /**
         * @brief Insert or overwrite key-value pair (owns the key)
         * @param key Field name — content is copied into res; safe to let
         *        the source memory die afterwards
         * @param val Value to assign
         * @param res Resource for the key's heap buffer (pass
         *        Config::instance().resource() for the global default).
         *        Must outlive the Object — the key buffer deallocates back
         *        into res when the key is destroyed.
         */
        void insert(std::string_view key, Json val, std::pmr::memory_resource *res);
        /**
         * @brief Insert or overwrite entry
         * @param entry Pair of (String key, Json value)
         */
        void insert(Entry entry);

        /**
         * @brief Remove key
         * @param key Field name to remove
         * @return true if key existed and was removed
         */
        bool remove(std::string_view key);

    public:
        /**
         * @brief Compare by content (order-insensitive)
         * @param other Object to compare with
         * @return true if same size and all key-value pairs match
         * @note Allocation-free: keys are located through the internal
         *       lookup index / linear sweep, not a temporary sort buffer.
         *       Duplicate keys resolve to their first occurrence.
         */
        [[nodiscard]] bool operator==(const Object &other) const;
    };

}

#endif // INCLUDE_PJH_JSON_OBJECT_HPP
