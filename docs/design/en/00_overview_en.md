# Design Docs Overview

This folder contains a series of technical design notes for `pjh_json`.
The goal is to explain the performance and usability tradeoffs in detail,
with code-oriented reasoning and concrete implementation notes.

Read order:
1. `00_overview_en.md` - this file
2. `01_simd_optimization_en.md` - SIMD usage and padding strategy
3. `02_zero_copy_ownership_en.md` - zero-copy model and ownership rules
4. `03_inplace_construction_en.md` - in-place parsing and overwrite strategy
5. `04_fast_slow_paths_en.md` - fast vs slow paths in parsing
6. `05_memory_resource_en.md` - PMR design and allocation strategy
7. `06_string_parsing_en.md` - string parsing, escape handling, Unicode
8. `07_number_and_literal_en.md` - number parsing and literal matching
9. `08_object_array_layout_en.md` - container layout and access behavior

Scope and invariants:
- Parser is DOM-style and returns a `Document` owning the backing buffer.
- Strings are `std::string_view` into that buffer.
- Arrays and objects are PMR containers to enable custom allocators.
- Fast paths are aggressively optimized; slow paths are correct and rare.

If you want additional docs, a good next step is a "Serializer design" note
once serialization is implemented.
