# 0007. Block-compressed line index

- Status: Accepted
- Date: 2026-11-04

## Context
Files can have hundreds of millions of lines. A plain array of 64-bit offsets costs 8 bytes per line (800 MB for 100M lines).

## Decision
Line end offsets and match lists are stored in a `SortedU64Sequence`: blocks of 128 values with a 64-bit base and bit-packed deltas, in segmented storage that never moves, with a single writer and lock-free readers of the published prefix.

## Consequences
About 1 byte per line for typical logs; O(1)-ish random access with a block decode cache. More complex than a vector; covered by property and concurrency tests.

## Alternatives considered
- `std::vector<uint64_t>`: too much memory.
- External SIMD integer compression libraries: extra dependency for a small amount of code.
