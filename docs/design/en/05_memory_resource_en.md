# Memory Resource (PMR) Design

This document describes how `pjh_json` uses `std::pmr` to enable
predictable allocation and fast teardown.

## Why PMR
JSON parsing creates many small objects:
- Array and object nodes
- Vector growth
- Temporary allocations during parse

Using `std::pmr` allows:
- Custom allocator strategies
- Single-shot deallocation for entire DOM
- Reduced fragmentation

## Core Containers
- `Array::Vec` is `std::pmr::vector<Json>`
- `Object::Vec` is `std::pmr::vector<std::pair<std::string_view, Json>>`

Both `Array` and `Object` store the resource pointer and use it in their
internal `Impl` allocations.

## Resource Flow
The parser accepts an optional `std::pmr::memory_resource*` parameter:
- `Parser` stores the resource and passes it into `Array`/`Object`.
- `parse_copy`, `parse_in_situ`, `parse_file` all accept a resource.

This gives callers control over the entire allocation graph.

## Typical High-Performance Setup
In benchmarks, a two-layer resource is used:
- `std::pmr::monotonic_buffer_resource` over a large contiguous buffer
- `std::pmr::unsynchronized_pool_resource` on top for pooled small blocks

Benefits:
- O(1) deallocation for the whole parse (reset or destroy the pool).
- Reduced fragmentation and cache misses.

## Object and Array Allocation
`Array` and `Object` use PIMPL-like `Impl` allocated via PMR:
- The `Impl` holds the `pmr::vector`.
- The `Impl` itself is allocated from the same resource.

This keeps all data near each other in memory.

## Design Tradeoffs
Using PMR adds:
- Slightly larger object size due to resource pointer.
- More complex constructor logic.

But it enables the core optimization: memory for the DOM can be
carved out of a single contiguous arena, which is critical for
high-throughput parsing.
