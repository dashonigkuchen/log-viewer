#!/usr/bin/env python3
"""Compare a Google Benchmark JSON result with a baseline and fail on slowdowns.

Usage:
  bench_compare.py RESULT.json BASELINE.json [--threshold 0.25]

For every benchmark in the baseline, the real time of the result is compared with the
baseline. When repetitions were used, the median aggregate is preferred, otherwise the mean of
all iterations. The script exits with 1 if any benchmark is slower than baseline * (1 +
threshold) or is missing from the result, 0 otherwise. Benchmarks only present in the result
are listed but do not fail. See docs/PERFORMANCE.md §4.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

_UNIT_TO_NS = {"ns": 1.0, "us": 1e3, "ms": 1e6, "s": 1e9}


def load_times(path: Path) -> dict[str, float]:
    """Returns benchmark name -> real time in nanoseconds."""
    data = json.loads(path.read_text(encoding="utf-8"))
    iterations: dict[str, list[float]] = {}
    medians: dict[str, float] = {}
    for entry in data.get("benchmarks", []):
        if entry.get("error_occurred"):
            continue
        unit = _UNIT_TO_NS.get(entry.get("time_unit", "ns"))
        if unit is None:
            raise ValueError(f"unknown time unit in {path}: {entry.get('time_unit')}")
        time_ns = float(entry["real_time"]) * unit
        if entry.get("run_type") == "aggregate":
            if entry.get("aggregate_name") == "median":
                medians[entry["run_name"]] = time_ns
            continue
        name = entry.get("run_name", entry["name"])
        iterations.setdefault(name, []).append(time_ns)
    times = {name: sum(values) / len(values) for name, values in iterations.items()}
    times.update(medians)
    return times


def compare(result: dict[str, float], baseline: dict[str, float], threshold: float) -> tuple[bool, list[str]]:
    """Returns (ok, report lines)."""
    ok = True
    lines = [f"{'benchmark':50} {'baseline':>12} {'result':>12} {'change':>8}"]
    for name in sorted(baseline):
        base = baseline[name]
        if name not in result:
            ok = False
            lines.append(f"{name:50} {base / 1e6:10.3f}ms {'missing':>12} {'FAIL':>8}")
            continue
        current = result[name]
        change = (current - base) / base if base > 0 else 0.0
        failed = change > threshold
        ok = ok and not failed
        status = "FAIL" if failed else "ok"
        lines.append(f"{name:50} {base / 1e6:10.3f}ms {current / 1e6:10.3f}ms {change:+7.1%} {status}")
    for name in sorted(set(result) - set(baseline)):
        lines.append(f"{name:50} {'new':>12} {result[name] / 1e6:10.3f}ms")
    return ok, lines


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("result", type=Path)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("--threshold", type=float, default=0.25, help="allowed slowdown (default 0.25 = 25%%)")
    args = parser.parse_args(argv)
    ok, lines = compare(load_times(args.result), load_times(args.baseline), args.threshold)
    print("\n".join(lines))
    print("bench_compare: OK" if ok else f"bench_compare: FAILED (threshold {args.threshold:.0%})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
