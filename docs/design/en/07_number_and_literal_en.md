# Number Parsing and Literal Matching

This document covers how numbers, booleans, and null are parsed with
fast paths and minimal overhead.

## Number Parsing Strategy
The parser distinguishes two cases:
1. Integer-only numbers without exponent
2. Float or long integer numbers requiring a slower path

### Fast Integer Path
Algorithm:
- Check and consume a leading `-`.
- Accumulate digits into a `uint64_t` in a tight loop.
- Track digit count to prevent overflow.
- Return `int64_t` result directly.

This avoids `std::from_chars` setup and keeps the common integer case
very fast.

### Slow Float Path
Triggered when:
- A `.` is seen
- An `e` or `E` is seen
- The integer digit count exceeds 18

The parser then:
- Scans the rest of the number to advance `m_curr`.
- Calls `std::from_chars` to parse into `double`.
- Returns a float JSON value.

This ensures correctness for all JSON number forms.

## Literal Parsing Strategy
JSON literals are `true`, `false`, and `null`.
The parser handles them by:
- Reading 4 or 8 bytes at once
- Comparing to machine-endian constants
- Using masks to ignore padding for `false`

### Why This Is Fast
The typical character-by-character checks are replaced by:
- One 32-bit compare for `true` / `null`
- One masked 64-bit compare for `false`

This reduces branch count and tight-loop overhead.

### Error Handling
If no literal matches:
- The parser throws `JSON Parse Error: Invalid literal`

## Result Types
The numeric and literal outputs map to:
- `true` / `false` -> `bool`
- `null` -> `std::nullptr_t`
- Integer -> `int64_t`
- Float -> `double`

The `Json` variant preserves this typing, which allows:
- Exact integer storage for small numbers
- `double` for float or large integer cases

## Tradeoffs
This approach prioritizes:
- Fast integer parsing
- Low overhead in literal matching

While still guaranteeing correctness for:
- Floating numbers
- Large integers
- JSON-compliant literals
