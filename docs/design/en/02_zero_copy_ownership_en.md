# Zero-Copy and Ownership Model

This document explains how `pjh_json` achieves zero-copy string handling
and what ownership rules callers must follow.

## Core Idea
Parsed JSON strings are stored as `std::string_view` that point directly
into the input buffer. This avoids:
- Allocations per string
- Duplicating character data
- Per-string memory management overhead

The tradeoff is that the input buffer must remain alive as long as any
`Json` values or `string_view` are used.

## Document as the Owner
The parser returns a `Document`, which:
- Inherits from `Json` (the parsed root value)
- Owns a `std::pmr::string buffer`

This buffer is the authoritative storage for all string data. As long as
the `Document` is alive, all `Json::as_string()` calls are safe.

## Parsing APIs and Ownership
Three entry points exist:

1. `parse_copy(json_text, res)`
   - Copies input into a new PMR string.
   - Adds 64 bytes of `\0` padding.
   - Returns a `Document` owning the buffer.

2. `parse_in_situ(buffer, res)`
   - Takes ownership of a caller-provided PMR string.
   - Requires `buffer.size() >= 64` extra bytes for padding.
   - Modifies the buffer in-place (escapes are rewritten).

3. `parse_file(path, res)`
   - Reads file into a PMR string and adds padding.
   - Returns a `Document` that owns the loaded contents.

## String Lifetimes
`Json` stores strings as `std::string_view`. This implies:
- No allocations for individual string values.
- The view is invalid once the `Document` is destroyed.

Caller rule:
- Do not store `std::string_view` or `Json` references beyond the
  lifetime of their originating `Document`.

If a long-lived copy is needed:
- Materialize a `std::string` in user code.

## Object Keys Are Also Views
`Object::Entry` is `std::pair<std::string_view, Json>`.
Keys are views into the same buffer, so key strings obey the same
lifetime rules as value strings.

## Mutation and Safety
Because strings are views into the backing buffer:
- Mutating the buffer after parse is not supported.
- Re-parsing into the same buffer is not supported.

In short: ownership is centralized in `Document`, and all views are
borrowed from it. This design trades memory overhead for speed and
cache locality, which is the primary goal of the library.
