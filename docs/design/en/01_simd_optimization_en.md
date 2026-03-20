# SIMD Optimization Details

This document explains how SIMD is used in the parser, why the 64-byte
padding exists, and which branches are deliberately structured to keep
the hot path predictable.

## Why SIMD Here
The JSON grammar contains large regions of characters that can be skipped
quickly. The two most expensive areas in a parser are:
- Whitespace skipping
- String scanning (finding `"` and `\` quickly)

SIMD allows these operations to examine 16/32/64 bytes at once, turning
branch-heavy loops into data-parallel comparisons.

## Padding Strategy
The parser assumes the input buffer has at least 64 bytes of trailing
zero padding. This is enforced by:
- `parse_copy`: resizes a PMR string to `json.size() + 64` and fills with `\0`
- `parse_in_situ`: requires caller-provided buffer with size >= 64 extra bytes
- `parse_file`: reads file content into a PMR string with extra padding

Benefits:
- SIMD loads can read past the logical end without an out-of-bounds fault.
- The parser can treat a `\0` as an artificial terminator for detection.
- Many `m_curr < m_end` checks are removed from tight loops.

## Whitespace Skipping (`Parser::skip_whitespace`)
Key idea: in valid JSON, all structural tokens and literals are `> 0x20`.
Only four whitespace characters are `<= 0x20`: space, `\t`, `\r`, `\n`.

Hot path:
- If `*m_curr > 0x20`, return immediately with no SIMD.

SIMD path:
- Load a SIMD batch of bytes.
- Compute `is_ws = (b <= 0x20) & (b != 0)`.
- Create a bitmask from the compare results.
- Invert the mask to find any non-whitespace or zero padding.
- Use `std::countr_zero` to skip directly to the first non-ws byte.

Why it is fast:
- The loop body has no unpredictable branches.
- The mask on `b != 0` treats `\0` padding as a stop signal.

## String Boundary Scan (`Parser::parse_string`)
For a JSON string, we need to find:
- A terminating quote `"`.
- An escape character `\` (slow path).
- Any illegal control character `< 0x20`.

SIMD path:
- Load a SIMD batch as `uint8_t`.
- Compare in parallel to `"`, `\`, and `< 0x20`.
- Combine masks; if any match, locate the first match and branch.

This is intentionally biased for the common case:
- Most strings contain no escapes or control characters.
- Many bytes can be skipped without per-byte branching.

Slow path entry:
- If the first match is a `\`, parsing switches to an in-situ rewrite loop.
- If the first match is `< 0x20`, a parse error is thrown.

## Literal Parsing (`Parser::parse_literal`)
Literal keywords are matched by comparing 4 or 8 bytes at once.
The implementation:
- Uses `std::bit_cast` to build machine-endian constants at compile time.
- Reads 4 or 8 bytes with `std::memcpy` to avoid strict aliasing issues.
- Uses a mask for the 8-byte `false` check to ignore trailing padding.

This avoids multiple `if (c == 't') ...` chains in the hot path.

## Design Tradeoffs
SIMD benefits come with constraints:
- The input must be contiguous and padded.
- Some logic is more complex due to mask math.
- The implementation currently relies on `xsimd`.

These tradeoffs are accepted because JSON parsing is a throughput-heavy
task, and SIMD accelerates the dominant cost centers without sacrificing
correctness.
