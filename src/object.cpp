#include "pjh_json/object.hpp"
#include "pjh_json/json.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <new>
#include <string_view>
#include <utility>

namespace pjh::json
{
    namespace
    {
        /*
         * Sentinel pair for Object::iterator's cached proxy
         *
         * A reference member must be bound at construction, and the end
         * iterator has no entry to bind to; its cache binds to this legal
         * empty pair and is never read (deref of end is UB by contract).
         * The sentinel is TU-local (a function-local static), not a
         * per-iterator member: a by-value Json cannot be declared in
         * object.hpp, where Json is still incomplete (json.hpp includes
         * object.hpp).
         */
        struct IteratorSentinel
        {
            String key{};
            Json val{};
        };

        IteratorSentinel &iterator_sentinel()
        {
            static IteratorSentinel s;
            return s;
        }

        /*
         * Empty-slot sentinel for Object::Index.
         *
         * Slots store `entry position + 1`, so zero is a safe "free" marker
         * without having to reserve a valid in-range position.
         */
        constexpr uint32_t kEmptySlot = 0;

        /*
         * Deterministic 32-bit FNV-1a over the key bytes.
         *
         * std::hash<std::string_view> is implementation-defined and not
         * stable across translation units; a fixed hash keeps the index
         * self-contained. Equality still reads the live entry keys (same
         * byte-content rule as String::operator==(string_view)), so the
         * hash is only a bucket selector.
         */
        [[nodiscard]] uint32_t key_hash(std::string_view key) noexcept
        {
            uint32_t h = 2166136261u;
            for (const char c : key)
            {
                h ^= static_cast<uint8_t>(c);
                h *= 16777619u;
            }
            return h;
        }
    }

    /*
     * Flat open-addressing key -> entry-position index (cache only).
     *
     * One contiguous slot array with linear probing: no per-key node
     * allocation, cache friendly. Slots hold positions, not key views, so
     * they survive entry-vector reallocation and the String moves a
     * reallocation performs; comparisons read the live keys, which also
     * covers owned (escaped) keys unchanged. The slot buffer is a pmr
     * container allocated from the object's own resource.
     */
    struct Object::Index
    {
        Index(std::pmr::memory_resource *res, uint32_t capacity)
            : cap(capacity), slots(res)
        {
            slots.assign(cap, kEmptySlot);
        }

        uint32_t cap;                       ///< power of two, > 0
        uint32_t size{0};                   ///< occupied slots (unique keys)
        bool stale{false};                  ///< entries changed since last refill
        std::pmr::vector<uint32_t> slots;   ///< position+1; kEmptySlot = free
    };
    /*
     * Construct empty Object with pmr allocator
     *
     * 1. Resolve allocator (default to global config resource).
     * 2. Initialize internal vector with the resolved resource.
     */
    Object::Object(std::pmr::memory_resource *res)
        : m_data(res ? res : Config::instance().resource()),
          m_resource(res ? res : Config::instance().resource())
    {
    }

    /*
     * Adopt existing vector
     *
     * Infer resource from vector's allocator, then move data in.
     */
    Object::Object(Vec val)
        : m_data(std::move(val)),
          m_resource(m_data.get_allocator().resource())
    {
    }

    Object::~Object()
    {
        index_free();
    }

    /*
     * Deep copy each entry into a new Object with the target resource
     *
     * 1. Materialise key string into target resource.
     * 2. Recursively clone value.
     * 3. Materialise the lookup index over the result (clone is already
     *    O(n); the index keeps later lookups O(1) and moves out with the
     *    returned object).
     */
    Object Object::clone(std::pmr::memory_resource *into) const
    {
        if (!into)
            into = Config::instance().resource();
        Object out(into);
        out.m_data.reserve(m_data.size());
        for (const auto &[key, val] : m_data)
        {
            String k{static_cast<std::string_view>(key)};
            k.own(into);
            out.m_data.emplace_back(std::move(k), val.clone(into));
        }
        if (out.m_data.size() > kIndexThreshold)
            out.index_build();
        return out;
    }

    /*
     * Move construct — steal vector from source
     *
     * The source keeps its resource: its moved-from state (allocator
     * member still bound) stays consistent with m_resource, so it remains
     * adoptable (Json heap_alloc / destroy, json.hpp). The index (if any)
     * transfers with the stolen storage — its slots hold positions, which
     * stay valid — and the source is left without one.
     */
    Object::Object(Object &&other) noexcept
        : m_data(std::move(other.m_data)),
          m_resource(other.m_resource),
          m_index(other.m_index)
    {
        other.m_index = nullptr;
    }

    /*
     * Move assign
     *
     * pmr allocators never propagate on move-assign (all
     * propagate_on_container_* traits are false), so libstdc++ steals the
     * source's storage only when the allocators compare equal; otherwise it
     * element-wise moves into a fresh buffer allocated by this container's
     * own allocator, leaving the source with its own (now empty) buffer.
     * m_data's allocator member is self-consistent in both cases, so only
     * the wrapper's m_resource bookkeeping must be guarded:
     *
     * 1. Guard against self-assignment.
     * 2. Compare allocators before the move (mirrors libstdc++'s own
     *    steal decision in vector::_M_move_assign).
     * 3. Move the vector, then explicitly clear the source. On the
     *    unequal-allocator path the standard leaves the source "valid but
     *    unspecified": libstdc++ empties it, but libc++/MSVC keep the
     *    moved-from entries in place (non-empty). The wrapper's contract
     *    (doxygen above) requires the moved-from source to be empty, so
     *    clear() it portably; it is a no-op when already empty and never
     *    deallocates the buffer (the source stays adoptable). clear() is
     *    noexcept, preserving the enclosing noexcept.
     * 4. Same resource: storage was stolen and both sides stay bound to
     *    the shared resource, so m_resource is copied (value-identical)
     *    instead of transferred: the source keeps it too, so a moved-from
     *    container remains adoptable (Json heap_alloc / destroy, json.hpp)
     *    into a node allocated in that shared resource.
     *    Different resources: keep this->m_resource (== m_data's allocator
     *    resource) so the node this container later heap-allocates into
     *    (Json::heap_alloc / Json::destroy, json.hpp) frees through the
     *    resource that owns this container's storage. The source keeps its
     *    m_resource because it keeps an empty buffer in that resource.
     *
     * Index handling mirrors the storage decision: the old index is always
     * released first (its positions describe the overwritten data). On the
     * same-resource steal path the source index transfers with the storage
     * (positions stay valid); on the element-wise path the source index is
     * released too — its positions describe storage this object does not
     * own — and the target stays lazy (rebuilt by the next write).
     */
    Object &Object::operator=(Object &&other) noexcept
    {
        if (this == &other)
            return *this;
        index_free();
        const bool same_resource =
            m_data.get_allocator() == other.m_data.get_allocator();
        m_data = std::move(other.m_data);
        other.m_data.clear();
        if (same_resource)
        {
            m_resource = other.m_resource;
            m_index = other.m_index;
            other.m_index = nullptr;
        }
        else
        {
            other.index_free();
        }
        return *this;
    }

    /*
     * First entry position whose key equals `key`, or npos.
     *
     * Uses the flat index when present (amortised O(1)); otherwise the
     * original linear first-match sweep. Never allocates, so it is safe
     * from the noexcept contains(). Comparison is the same byte-content
     * rule as the old `kv.first == key`.
     */
    size_t Object::find_slot(std::string_view key, bool rebuild) const noexcept
    {
        if (m_index != nullptr && (rebuild || !m_index->stale))
        {
            // An erase invalidates every stored position; refill once here
            // (no allocation) rather than on every erase. With `rebuild`
            // false the caller prefers the linear sweep over that cost.
            if (m_index->stale)
                index_refill(*m_index);
            const Index &idx = *m_index;
            const size_t mask = idx.cap - 1;
            size_t slot = key_hash(key) & mask;
            while (idx.slots[slot] != kEmptySlot)
            {
                const size_t pos = idx.slots[slot] - 1;
                if (static_cast<std::string_view>(m_data[pos].first) == key)
                    return pos;
                slot = (slot + 1) & mask;
            }
            return npos;
        }
        for (size_t i = 0; i < m_data.size(); ++i)
        {
            if (static_cast<std::string_view>(m_data[i].first) == key)
                return i;
        }
        return npos;
    }

    /*
     * Index lifecycle helpers.
     */
    void Object::index_insert(Index &idx, size_t pos) const noexcept
    {
        const std::string_view key =
            static_cast<std::string_view>(m_data[pos].first);
        const size_t mask = idx.cap - 1;
        size_t slot = key_hash(key) & mask;
        while (idx.slots[slot] != kEmptySlot)
        {
            // First occurrence wins: an adopted vector may carry duplicate
            // keys, and the linear sweep returns the earliest too.
            if (static_cast<std::string_view>(m_data[idx.slots[slot] - 1].first) == key)
                return;
            slot = (slot + 1) & mask;
        }
        idx.slots[slot] = static_cast<uint32_t>(pos) + 1;
        ++idx.size;
    }

    void Object::index_refill(Index &idx) const noexcept
    {
        std::fill(idx.slots.begin(), idx.slots.end(), kEmptySlot);
        idx.size = 0;
        idx.stale = false;
        for (size_t i = 0; i < m_data.size(); ++i)
            index_insert(idx, i);
    }

    void Object::index_ensure() const
    {
        // Lazy cache completion for read-only callers: never touch small
        // objects (the bounded linear sweep beats hashing there) and never
        // rebuild an index that already exists (even a stale one — the
        // lookup path refills that in place). May throw bad_alloc; the
        // caller (operator==) is not noexcept.
        if (m_index == nullptr && m_data.size() > kIndexThreshold)
            index_build();
    }

    void Object::index_build() const
    {
        // Drop the old index first: everything past this point may throw,
        // and a null cache is always a correct (linear) fallback.
        index_free();
        size_t cap = 32;
        while (cap < m_data.size() * 2)
            cap <<= 1;

        auto alloc = std::pmr::polymorphic_allocator<Index>(m_resource);
        Index *fresh = alloc.allocate(1);
        try
        {
            ::new (static_cast<void *>(fresh))
                Index(m_resource, static_cast<uint32_t>(cap));
        }
        catch (...)
        {
            alloc.deallocate(fresh, 1);
            throw;
        }
        index_refill(*fresh);
        m_index = fresh;
    }

    void Object::index_note_append(size_t pos)
    {
        if (m_index == nullptr)
        {
            if (m_data.size() > kIndexThreshold)
                index_build();
            return;
        }
        if (m_index->stale)
        {
            // The refill re-reads every key, including the just-appended
            // `pos`, so it supersedes a single insert.
            index_refill(*m_index);
            return;
        }
        // Keep load <= 0.5; a rebuild re-reads every key (including the
        // just-appended `pos`), so no separate insert is needed.
        if ((static_cast<size_t>(m_index->size) + 1) * 2 > m_index->cap)
        {
            index_build();
            return;
        }
        index_insert(*m_index, pos);
    }

    /*
     * Erase maintenance.
     *
     * Erase shifts every later entry down by one, so every stored position
     * past the erased one is stale. Refilling on each erase would cost an
     * O(cap) cache-cold walk per call while giving no asymptotic benefit
     * (erase is already O(n) from the vector shift), so the index is just
     * marked stale: the next lookup refills it in place once. Below the
     * threshold the index is dropped.
     */
    void Object::index_after_erase() noexcept
    {
        if (m_index == nullptr)
            return;
        if (m_data.size() <= kIndexThreshold)
            index_free();
        else
            m_index->stale = true;
    }

    void Object::index_free() const noexcept
    {
        if (m_index == nullptr)
            return;
        Index *idx = m_index;
        m_index = nullptr;
        std::pmr::polymorphic_allocator<Index> alloc(m_resource);
        std::destroy_at(idx);
        alloc.deallocate(idx, 1);
    }

    /*
     * Key lookup via find_slot (O(1) with an index, linear otherwise).
     */
    bool Object::contains(std::string_view key) const noexcept
    {
        return find_slot(key) != npos;
    }

    /*
     * Mutable key access: find-or-insert
     *
     * 1. Search for existing key.
     * 2. If found, return reference to value.
     * 3. If not found, append default-constructed Json entry (and maintain
     *    the index) and return it.
     */
    Json &Object::operator[](std::string_view key)
    {
        const size_t pos = find_slot(key);
        if (pos != npos)
            return m_data[pos].second;

        m_data.emplace_back(key, Json());
        index_note_append(m_data.size() - 1);
        return m_data.back().second;
    }

    /*
     * Const key access: find-or-throw
     *
     * 1. Search for key.
     * 2. If found, return const reference.
     * 3. If missing, throw out_of_range.
     */
    const Json &Object::operator[](std::string_view key) const
    {
        const size_t pos = find_slot(key);
        if (pos == npos)
            throw std::out_of_range("json key not found");

        return m_data[pos].second;
    }

    /*
     * Mutable key access with bounds check: find-or-throw
     *
     * 1. Search for key.
     * 2. If found, return mutable reference.
     * 3. If missing, throw out_of_range.
     */
    Json &Object::at(std::string_view key)
    {
        const size_t pos = find_slot(key);
        if (pos == npos)
            throw std::out_of_range("json key not found");

        return m_data[pos].second;
    }

    /*
     * Const key access with bounds check: find-or-throw
     *
     * 1. Search for key.
     * 2. If found, return const reference.
     * 3. If missing, throw out_of_range.
     */
    const Json &Object::at(std::string_view key) const
    {
        const size_t pos = find_slot(key);
        if (pos == npos)
            throw std::out_of_range("json key not found");

        return m_data[pos].second;
    }

    /*
     * Insert or overwrite by key
     *
     * 1. Search for existing key.
     * 2. If found, overwrite its value (key and position unchanged, so the
     *    index stays valid).
     * 3. If not found, append new entry and note the append to the index.
     */
    void Object::insert(std::string_view key, Json val)
    {
        const size_t pos = find_slot(key);
        if (pos != npos)
        {
            m_data[pos].second = std::move(val);
            return;
        }
        m_data.emplace_back(key, std::move(val));
        index_note_append(m_data.size() - 1);
    }

    /*
     * Insert or overwrite by key, owning the key
     *
     * 1. Resolve resource (null falls back to global config resource).
     * 2. Search for existing key (content compare, mode-agnostic).
     * 3. If found, overwrite its value; keep the old key. The lookup
     *    happens before any owned-key allocation, so a hit does not
     *    allocate.
     * 4. If not found, copy the key into an owned String via own(res) and
     *    append; note the append to the index. The key buffer is freed back
     *    into res at destruction, so res must outlive this Object.
     */
    void Object::insert(std::string_view key, Json val,
                        std::pmr::memory_resource *res)
    {
        if (!res)
            res = Config::instance().resource();

        const size_t pos = find_slot(key);
        if (pos != npos)
        {
            m_data[pos].second = std::move(val);
            return;
        }

        String owned{key};
        owned.own(res);
        m_data.emplace_back(std::move(owned), std::move(val));
        index_note_append(m_data.size() - 1);
    }

    void Object::insert(Entry entry)
    {
        const size_t pos = find_slot(entry.first);
        if (pos != npos)
        {
            m_data[pos].second = std::move(entry.second);
            return;
        }
        m_data.push_back(std::move(entry));
        index_note_append(m_data.size() - 1);
    }

    /*
     * Remove key
     *
     * 1. Search for key. A stale index is bypassed for the linear sweep:
     *    remove-heavy workloads would otherwise refill it on every erase.
     * 2. If found, erase entry and return true; the index is marked stale
     *    (rebuilt lazily by the next lookup) or dropped below threshold.
     * 3. If missing, return false.
     */
    bool Object::remove(std::string_view key)
    {
        const size_t pos = find_slot(key, /*rebuild=*/false);
        if (pos == npos)
            return false;

        m_data.erase(m_data.begin() +
                     static_cast<Vec::difference_type>(pos));
        index_after_erase();
        return true;
    }

    /*
     * Deep merge (see the header doxygen).
     *
     * 1. Direct self-merge is a defined no-op (guard); other shared
     *    storage is UB (doxygen @warning).
     * 2. Iterate other.m_data (const arg: never invalidated by *this
     *    mutation); find_slot is the index-aware lookup, no iterator is
     *    held across a m_data change.
     * 3. Existing key: nested Objects merge recursively (no clone);
     *    every other kind replaces wholesale, cloned into this resource
     *    (null is a value: it overwrites).
     * 4. Missing key: append with an OWNED key, then index_note_append —
     *    an existing index must learn the new position or later lookups
     *    false-miss (correctness, not just speed).
     */
    void Object::merge(const Object &other)
    {
        if (this == &other)
            return;
        for (const Entry &kv : other.m_data)
        {
            const size_t pos = find_slot(kv.first);
            if (pos != npos)
            {
                if (m_data[pos].second.is_object() && kv.second.is_object())
                    m_data[pos].second.as_object().merge(kv.second.as_object());
                else
                    m_data[pos].second = kv.second.clone(m_resource);
            }
            else
            {
                String owned{static_cast<std::string_view>(kv.first)};
                owned.own(m_resource);
                m_data.emplace_back(std::move(owned),
                                    kv.second.clone(m_resource));
                index_note_append(m_data.size() - 1);
            }
        }
    }

    /*
     * Content equality (order-insensitive), allocation-free per comparison
     * once the lookup indices exist.
     *
     * 1. Fast reject on size mismatch.
     * 2. If neither side has an index and the (equal) size is above
     *    kIndexThreshold, materialise the index on the side we probe
     *    (other) through the mutable cache: one amortised O(n) build turns
     *    the per-key linear sweep — O(n^2) overall — into O(1) probes.
     *    Below the threshold it is a no-op and the bounded ≤256 short-key
     *    sweep stays.
     * 3. For every entry of one side, find the same key on the other side
     *    and compare values. find_slot never allocates and never throws; it
     *    is O(1) when the looked-up side has a materialised index, else a
     *    linear first-match sweep.
     * 4. Always look *into* whichever side owns an index: iterating the
     *    indexed side would pay a linear sweep per entry.
     *
     * Duplicate keys (only an adopted Object(Vec) can carry them) resolve to
     * the first occurrence, making the result deterministic instead of the
     * old std::sort's unspecified order — but, when the iterated side
     * carries duplicates, potentially asymmetric (documented on the header).
     */
    bool Object::operator==(const Object &other) const
    {
        if (size() != other.size())
            return false;

        if (m_index == nullptr && other.m_index == nullptr)
            other.index_ensure();

        if (other.m_index != nullptr || m_index == nullptr)
        {
            for (const Entry &e : m_data)
            {
                const size_t pos = other.find_slot(e.first);
                if (pos == npos || other.m_data[pos].second != e.second)
                    return false;
            }
        }
        else
        {
            for (const Entry &e : other.m_data)
            {
                const size_t pos = find_slot(e.first);
                if (pos == npos || m_data[pos].second != e.second)
                    return false;
            }
        }
        return true;
    }

    size_t Object::size() const noexcept { return m_data.size(); }
    bool Object::empty() const noexcept { return m_data.empty(); }

    void Object::clear() noexcept
    {
        index_free();
        m_data.clear();
    }

    /*
     * Non-const track: the wrapper seals the key side.
     *
     * 1. operator* builds a fresh EntryRef per call (zero staleness).
     * 2. operator->/operator++ re-bind the cached proxy to the current
     *    entry before returning/advancing (m_it may reach end after ++,
     *    and dereferencing end is UB — so the re-bind reads the
     *    pre-advance position). The re-bind is a destroy/recreate cycle:
     *    EntryRef's reference members are not assignable.
     * 3. The end iterator's cache binds to the sentinel empty pair at
     *    construction and is never read (deref of end is UB by contract).
     */
    Object::iterator::iterator(Vec::iterator it) noexcept
        : m_it(it),
          m_ref(iterator_sentinel().key, iterator_sentinel().val)
    {
    }

    Object::EntryRef Object::iterator::operator*() const noexcept
    {
        return EntryRef{m_it->first, m_it->second};
    }

    const Object::EntryRef *Object::iterator::operator->() const noexcept
    {
        std::destroy_at(&m_ref);
        ::new (static_cast<void *>(&m_ref))
            Object::EntryRef{m_it->first, m_it->second};
        return &m_ref;
    }

    Object::iterator &Object::iterator::operator++() noexcept
    {
        std::destroy_at(&m_ref);
        ::new (static_cast<void *>(&m_ref))
            Object::EntryRef{m_it->first, m_it->second};
        ++m_it;
        return *this;
    }

    bool operator==(const Object::iterator &a, const Object::iterator &b) noexcept
    {
        return a.m_it == b.m_it;
    }

    bool operator!=(const Object::iterator &a, const Object::iterator &b) noexcept
    {
        return a.m_it != b.m_it;
    }

    Object::iterator Object::begin() noexcept { return iterator{m_data.begin()}; }
    Object::iterator Object::end() noexcept { return iterator{m_data.end()}; }
    Object::Vec::const_iterator Object::begin() const noexcept { return m_data.begin(); }
    Object::Vec::const_iterator Object::end() const noexcept { return m_data.end(); }

    /*
     * Zero-allocation projection views over the entry vector.
     *
     * Order = the vector order (insertion order; a duplicate-key overwrite
     * keeps the first occurrence's position — the last-wins semantics).
     * The views hold a pointer into m_data: any storage-changing operation
     * (insert/remove/clear, a move-assign) invalidates them.
     */
    KeysView Object::keys() const noexcept { return KeysView{&m_data}; }
    ValuesView Object::values() noexcept { return ValuesView{&m_data}; }
    ConstValuesView Object::values() const noexcept { return ConstValuesView{&m_data}; }
}
