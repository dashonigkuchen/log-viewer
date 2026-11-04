# 0005. Positional reads instead of memory mapping

- Status: Accepted
- Date: 2026-11-04

## Context
Log files are written, rotated, and truncated by other processes while the viewer reads them.

## Decision
Files are opened read-only with full sharing and read with positional reads (`ReadFile` with an offset, `pread`) into reusable buffers. No memory mapping.

## Consequences
On Windows, an open file mapping makes `SetEndOfFile` in the writer fail, which breaks truncating log rotation; access to truncated mapped pages raises access violations / `SIGBUS`. Positional reads avoid both. Sequential reads with 4 MiB buffers reach the same throughput as mapping in practice; this is verified by `BM_ReadOnly_Sequential` versus the indexer.

## Alternatives considered
Memory mapping: simpler code for random access, but the problems above are unacceptable for live logs.
