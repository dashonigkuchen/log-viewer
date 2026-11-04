# 0008. Own thread pool instead of TBB

- Status: Accepted
- Date: 2026-11-04

## Context
Searching and indexing need parallelism and cancellation.

## Decision
A small fixed-size thread pool based on `std::jthread` and `std::stop_token`, with an ordered result reorder buffer in the search engine.

## Consequences
One dependency less; behavior is fully under our control and testable with TSan. Work stealing and flow graphs are not available; the workloads (large independent chunks) do not need them.

## Alternatives considered
oneTBB: powerful, but heavy for this use, and vcpkg/CI build time increases.
