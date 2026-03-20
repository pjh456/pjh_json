# In-Place Construction Strategy

This document describes how `pjh_json` constructs DOM nodes in-place,
minimizes temporary objects, and uses overwrite parsing for arrays and
objects.

## In-Place Parsing Goals
The parser aims to:
- Avoid unnecessary temporaries.
- Reduce allocations.
- Build the final DOM layout directly in the target containers.

The primary technique is to allocate a node slot first, then parse
directly into it.

## Array Parsing In-Place
When parsing arrays, the parser uses this pattern:
1. Emplace a placeholder `Json(nullptr)` into the array.
2. Call `parse_value_inplace` with a reference to that slot.
3. The slot is overwritten with the real value.

This avoids:
- Constructing a temporary `Json` and moving it into the vector.
- Extra branches for different element types.

Code path:
- `Parser::parse_array_inplace(Json &out)`
- `Parser::parse_value_inplace(Json &out)`

## Object Parsing In-Place
Object parsing uses a similar approach:
1. Emplace an `Entry` with key and `Json(nullptr)` value.
2. Parse directly into `entry.second` via `parse_value_inplace`.

This pattern ensures that:
- Each element is constructed once.
- The final object layout is stable early.
- The `std::pmr::vector` grows only when needed.

## Reserve Strategy
Both array and object parsing call `reserve(4)` early:
- This is a small heuristic that avoids repeated growth for small objects.
- It is cheap and beneficial for typical JSON.

## In-Situ String Rewrite
When parsing a string with escapes:
- The parser switches to a slow path that rewrites escape sequences
  directly in the input buffer.
- A `dst` pointer writes characters in-place, collapsing escaped forms.

As a result:
- No new string is allocated.
- The parsed string content is contiguous and final in the buffer.

## Why This Matters
In-place construction reduces:
- Branching in tight loops
- The number of moves/copies
- Allocator pressure

This is a key piece of the "high performance + simple API" design goal:
the user sees a normal DOM, while the parser avoids unnecessary work.
