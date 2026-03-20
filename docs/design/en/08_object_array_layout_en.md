# Object and Array Layout

This document explains how arrays and objects are represented in memory,
and how their access semantics are designed.

## Array Layout
`Array` is a thin wrapper around:
- `std::pmr::vector<Json>`

Design choices:
- Contiguous storage for cache-friendly traversal.
- PMR-backed allocation for arena-style lifetime control.

Access:
- `operator[](size_t)` returns by reference without bounds checking.
- `at(size_t)` throws on out-of-range.

## Object Layout
`Object` is implemented as:
- `std::pmr::vector<std::pair<std::string_view, Json>>`

Design choices:
- Keys are `string_view` for zero-copy.
- Vector-based storage for fast iteration and locality.
- Linear lookup for keys to keep structure simple and compact.

Access semantics:
- `operator[](key)` inserts `Json()` if key is missing.
- `operator[](key) const` throws if key is missing.
- `at(key)` throws if key is missing.

This mirrors common JSON DOM behavior while keeping memory overhead low.

## Tradeoffs
Pros:
- Simple, compact representation.
- Excellent iteration performance.
- No hashing overhead.

Cons:
- Key lookup is O(n).

The design assumes that most objects are small to medium, and that
parsing throughput and memory locality are the primary goals.
