# String Parsing and Unicode Handling

This document details the JSON string parser, including SIMD scanning,
escape handling, and Unicode surrogate pairs.

## Overview
String parsing is split into two stages:
1. Fast scan using SIMD to find a special byte.
2. Slow in-situ rewrite if an escape is encountered.

This keeps the common case fast while still handling all JSON escape
rules correctly.

## Fast Scan
The parser treats the input as `uint8_t` to avoid signed-char issues.
It scans in SIMD batches to find any of:
- `"` (end of string)
- `\` (escape)
- `< 0x20` (illegal control character)

If the first match is a `"`:
- Return `std::string_view(start, len)` immediately.

If the first match is `\`:
- Switch to slow path (in-situ rewrite).

If the first match is `< 0x20`:
- Throw a parse error.

## In-Situ Rewrite (Slow Path)
When escapes are present, the parser:
- Uses `dst` (write pointer) and `m_curr` (read pointer).
- Rewrites escape sequences into their real byte values.
- Writes the result directly into the original buffer.

This guarantees the final string data is contiguous and requires no
extra allocations.

## Escape Handling
Supported escapes:
- `\"` `\\` `\/`
- `\b` `\f` `\n` `\r` `\t`
- `\uXXXX` (Unicode)

If an invalid escape is found, a parse error is thrown.

## Unicode and Surrogate Pairs
For `\uXXXX`:
- `parse_hex4()` reads four hex digits into a codepoint.

If the codepoint is a high surrogate (`0xD800` - `0xDBFF`):
- The parser expects another `\uXXXX`.
- The second must be a low surrogate (`0xDC00` - `0xDFFF`).
- They are combined into a single codepoint.

The codepoint is then encoded into UTF-8 using `encode_utf8`.

This yields correct UTF-8 output directly in the buffer.

## Control Characters
JSON disallows unescaped control characters in strings.
The parser enforces this by:
- Detecting `< 0x20` in SIMD scan.
- Checking each byte in the slow path.

If found, it throws an error.

## Resulting String View
After parsing:
- The returned `std::string_view` points into the same buffer.
- Its length is computed from the final `dst` pointer.

This gives zero-copy string values while still supporting escape
processing and Unicode correctly.
