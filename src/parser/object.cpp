#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <vector>

// The key-index slow paths below are deliberately kept out of line so the
// small-object scalar sweep stays small and register-friendly (measured:
// inlining them costs ~3% on objects that never build an index). C++20 has
// no standard spelling, so fall back to plain functions elsewhere.
#if defined(__GNUC__) || defined(__clang__)
#define PJH_JSON_NOINLINE [[gnu::noinline]]
#else
#define PJH_JSON_NOINLINE
#endif

namespace pjh::json
{
    namespace
    {
        /*
         * Entry count at which the per-key first-match scan switches from a
         * scalar sweep to a lazily materialised key->position index.
         *
         * Below the threshold the sweep is only a handful of short
         * string_view comparisons and beats any hashing (benchmark objects
         * average ~3 keys, so they never build the index). The index costs
         * a one-off build over the entries parsed so far, so it only pays
         * off once that build is amortised by enough indexed lookups; the
         * value is set above the measured crossover to keep medium objects
         * on the scalar path.
         */
        constexpr size_t kKeyIndexThreshold = 256;

        /*
         * Flat open-addressing key -> first-occurrence position index for
         * one object.
         *
         * A single contiguous slot array (entry_index + 1, 0 = empty) with
         * linear probing avoids the per-key node allocation of a node-based
         * unordered_map — measured ~10x cheaper to build. Positions, not
         * key views, are stored: comparisons read the live entry keys, so
         * the index stays correct across vector reallocation and String
         * moves, and covers owned (escaped) keys unchanged.
         */
        class KeyIndex
        {
        public:
            explicit KeyIndex(std::pmr::memory_resource *res)
                : m_slots(res), m_resource(res) {}

            KeyIndex(const KeyIndex &) = delete;
            KeyIndex &operator=(const KeyIndex &) = delete;
            KeyIndex(KeyIndex &&) = default;
            KeyIndex &operator=(KeyIndex &&) = default;

            // (Re)build over every entry currently stored in obj.
            PJH_JSON_NOINLINE void build(const Object &obj)
            {
                const auto &entries = obj.data();
                // 4x headroom keeps the load low so the build stays
                // collision-light and no rehash is needed until the object
                // has grown well past the threshold (the rehash would
                // otherwise land right in the threshold-adjacent window).
                size_t cap = 16;
                while (cap < entries.size() * 4)
                    cap <<= 1;
                m_slots.assign(cap, 0);
                m_mask = cap - 1;
                m_count = 0;
                for (size_t i = 0; i < entries.size(); ++i)
                    insert_slot(std::string_view(entries[i].first), i);
            }

            // First-occurrence position of `key`, or entries.size() when
            // unseen. On a miss `key` is stored at the position the following
            // append will use, so the index stays in sync.
            PJH_JSON_NOINLINE size_t find_or_insert(
                const Object &obj, std::string_view key)
            {
                const auto &entries = obj.data();
                size_t slot = hash(key) & m_mask;
                while (m_slots[slot] != 0)
                {
                    size_t idx = m_slots[slot] - 1;
                    if (entries[idx].first == key)
                        return idx;
                    slot = (slot + 1) & m_mask;
                }
                // Grow before storing the not-yet-appended position: all
                // currently stored positions refer to real entries, so it is
                // safe for grow() to re-read their keys.
                if ((m_count + 1) * 2 > m_slots.size())
                {
                    grow(obj);
                    slot = hash(key) & m_mask;
                    while (m_slots[slot] != 0)
                        slot = (slot + 1) & m_mask;
                }
                size_t pos = entries.size();
                m_slots[slot] = pos + 1;
                ++m_count;
                return pos;
            }

        private:
            static size_t hash(std::string_view key)
            {
                return std::hash<std::string_view>{}(key);
            }

            void insert_slot(std::string_view key, size_t idx)
            {
                size_t slot = hash(key) & m_mask;
                while (m_slots[slot] != 0)
                    slot = (slot + 1) & m_mask;
                m_slots[slot] = idx + 1;
                ++m_count;
            }

            PJH_JSON_NOINLINE void grow(const Object &obj)
            {
                const auto &entries = obj.data();
                std::pmr::vector<size_t> old(m_resource);
                old.swap(m_slots);
                size_t cap = old.size() * 2;
                m_slots.assign(cap, 0);
                m_mask = cap - 1;
                m_count = 0;
                for (size_t s = 0; s < old.size(); ++s)
                {
                    if (old[s] != 0)
                        insert_slot(
                            std::string_view(entries[old[s] - 1].first),
                            old[s] - 1);
                }
            }

            std::pmr::vector<size_t> m_slots;
            std::pmr::memory_resource *m_resource;
            size_t m_mask = 0;
            size_t m_count = 0;
        };
    }

    /*
     * Track seen keys via unordered_set for duplicate detection.
     *
     * If the key was already inserted, throw ParseError.
     * Only called when Config::strict_duplicate_keys() is enabled.
     */
    static void check_duplicate_key(
        std::string_view key,
        std::pmr::unordered_set<std::string_view> &seen)
    {
        if (!seen.insert(key).second)
            throw ParseError(
                std::string("Duplicate key \"") + std::string(key) + "\" in object");
    }

    /*
     * Parse JSON object in-place
     *
     * 1. Consume opening '{'.
     * 2. Skip whitespace; if '}' immediately -> empty object, return.
     * 3. Pre-allocate capacity hints. Duplicate-key tracking is lazily
     *    initialized only when Config::strict_duplicate_keys() is set; the
     *    non-strict first-match index is likewise lazy (large objects only).
     * 4. Loop:
     *    a. Parse a string key.
     *    b. Conditionally check for duplicate keys.
     *    c. Expect and consume ':' separator.
     *    d. Parse the value in-place; if the key already exists (strict
     *       off) overwrite the first occurrence's value, preserving its
     *       key and position — last-wins, as in Object::insert. With
     *       strict ON a duplicate already threw in (b), so every key is
     *       distinct and each is appended directly; the first-match scan
     *       is skipped entirely.
     *    e. Check for ',' (continue) or '}' (done).
     */
    void Parser::parse_object_inplace(Json &out)
    {
        if (m_max_depth != 0 && m_depth + 1 > m_max_depth)
            throw_parse_error("Maximum nesting depth exceeded", m_curr, m_begin);
        DepthFrame frame(*this);

        // Consume '{' and create object
        ++m_curr;
        Object obj(m_resource);
        skip_whitespace();

        // Early return for empty object
        if (*m_curr == '}')
        {
            ++m_curr;
            out = std::move(obj);
            return;
        }

        // Pre-allocate: the outermost container bounds its entry count
        // from the remaining input; nested containers keep the fixed hint.
        obj.data().reserve(initial_reserve(5, sizeof(Object::Entry)));
        std::optional<std::pmr::unordered_set<std::string_view>> seen;
        if (Config::instance().strict_duplicate_keys())
        {
            seen.emplace(m_resource);
            seen->reserve(8);
        }

        // Lazy key -> position index for the non-strict first-match scan.
        // Only materialised once the entry count reaches
        // kKeyIndexThreshold; small objects never touch it.
        std::optional<KeyIndex> key_index;

        while (true)
        {
            // Parse key
            skip_whitespace();
            if (*m_curr != '"')
                throw_parse_error("Expected string key in object", m_curr, m_begin);
            auto key = parse_string();
            if (seen)
                check_duplicate_key(key, *seen);

            // Parse colon separator
            skip_whitespace();
            if (*m_curr != ':')
                throw_parse_error("Expected ':' in object", m_curr, m_begin);
            ++m_curr;

            // Parse value — last-wins duplicate policy, mirroring
            // Object::insert: the first occurrence keeps its key and
            // position, its value is overwritten in place; append only
            // when the key is unseen. When strict is ON a duplicate
            // already threw above, so this scan block does not run at all
            // and every key takes the append path.
            auto &entries = obj.data();
            size_t pos = entries.size();
            // Strict mode: check_duplicate_key above already rejected any
            // repeated key, so every key here is distinct and the
            // first-match scan would always miss. Skip it (and never build
            // the lazy key index) and let the append path below run.
            if (!seen)
            {
                if (entries.size() >= kKeyIndexThreshold)
                {
                    // Indexed fast path for large objects; the index is built
                    // once, from the entries already stored, and kept in sync
                    // by find_or_insert on every append below.
                    if (!key_index)
                    {
                        key_index.emplace(m_resource);
                        key_index->build(obj);
                    }
                    pos = key_index->find_or_insert(obj, std::string_view(key));
                }
                else
                {
                    // Small-object scalar sweep: direct index access,
                    // first equal entry wins.
                    for (size_t i = 0; i < entries.size(); ++i)
                    {
                        if (entries[i].first == key)
                        {
                            pos = i;
                            break;
                        }
                    }
                }
            }
            if (pos < entries.size())
            {
                parse_value_inplace(entries[pos].second);
            }
            else
            {
                entries.emplace_back(std::move(key), Json(nullptr));
                parse_value_inplace(entries.back().second);
            }

            // Check for closing brace or comma
            skip_whitespace();
            if (m_curr >= m_end)
                throw_parse_error("Unexpected end of object", m_curr, m_begin);
            if (*m_curr == '}')
            {
                ++m_curr;
                out = std::move(obj);
                return;
            }
            if (*m_curr == ',')
                ++m_curr;
            else
                throw_parse_error("Expected ',' or '}' in object", m_curr, m_begin);
        }
    }
}

#undef PJH_JSON_NOINLINE
