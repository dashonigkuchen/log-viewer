# 0006. Qt-free core library

- Status: Accepted
- Date: 2026-11-04

## Context
The engine (indexing, encoding, search, change detection) is the performance- and correctness-critical part and must be tested thoroughly and fast.

## Decision
`lv_core` uses only the C++ standard library plus PCRE2, simdutf, and tl-expected. It does not link Qt; a CMake check enforces this. Qt glue lives in `lv_model`, widgets in `lv_ui`.

## Consequences
Core tests run without an event loop, under all sanitizers, and benchmarks are not affected by Qt. A thin adaptation layer is needed (strings, signals).

## Alternatives considered
Using QFile/QString in the core: less code, but couples the engine to Qt and makes it harder to optimize memory and threading.
