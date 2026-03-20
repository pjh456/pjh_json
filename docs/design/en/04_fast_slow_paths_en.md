# Fast vs Slow Paths

This document maps the main parsing stages into fast and slow paths,
and explains how the parser intentionally biases toward the fast path.

## General Principle
The parser assumes the common case:
- Mostly ASCII JSON
- Few escapes
- Reasonably sized numbers
- Regular whitespace

Fast paths are branch-light and use SIMD where possible.
Slow paths handle rare cases correctly (escapes, Unicode, long numbers).

## Whitespace Skipping
Fast path:
- If `*m_curr > 0x20`, return immediately.

Slow path:
- SIMD loop for blocks of whitespace.
- Only used when the immediate character is whitespace.

## String Parsing
Fast path:
- SIMD scan finds the closing `"` with no escapes or controls.
- Returns a `string_view` immediately.

Slow path:
- Triggered when `\` is encountered.
- Runs in-situ rewrite loop with escape handling.
- Handles Unicode escapes and surrogate pairs.

Error path:
- `< 0x20` control characters raise parse errors.
- `\0` padding before the closing quote raises "Unterminated string".

## Number Parsing
Fast path:
- Parses pure integers in a single loop.
- Builds a `uint64_t` and returns `int64_t`.

Slow path:
- If a decimal point, exponent, or too many digits appear,
  fallback to `std::from_chars` for `double`.

This avoids `from_chars` setup cost for the common integer case.

## Literal Parsing
Fast path:
- Reads 4 or 8 bytes at once and compares against precomputed constants.
- `true`, `false`, `null` are resolved without per-character loops.

Slow path:
- If neither constant matches, an error is raised.

## Object and Array
Fast path:
- Reserve a small capacity upfront.
- Parse directly into a pre-emplaced slot.

Slow path:
- Only slow if nested structures or deep recursion cause repeated growth.

## Design Intent
The fast path is not "optional optimization". It defines the primary
control flow for correct JSON, while the slow path is reserved for
special cases that cannot be handled by the fast path safely.

This structure keeps the average parse cost close to the theoretical
minimum for valid JSON without escapes.
