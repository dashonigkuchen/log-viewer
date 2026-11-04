"""Unit tests for tools/bench_compare.py. Run: python3 -I tools/tests/test_bench_compare.py"""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import tempfile
import unittest
from pathlib import Path

_SPEC = importlib.util.spec_from_file_location(
    "bench_compare", Path(__file__).resolve().parent.parent / "bench_compare.py"
)
assert _SPEC is not None and _SPEC.loader is not None
bench_compare = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(bench_compare)


def entry(name: str, real_time: float, unit: str = "ms", **extra: object) -> dict[str, object]:
    return {"name": name, "run_name": name, "run_type": "iteration", "real_time": real_time,
            "cpu_time": real_time, "time_unit": unit, **extra}


class BenchCompareTest(unittest.TestCase):
    def setUp(self) -> None:
        self._dir = tempfile.TemporaryDirectory()
        self.dir = Path(self._dir.name)

    def tearDown(self) -> None:
        self._dir.cleanup()

    def write(self, name: str, benchmarks: list[dict[str, object]]) -> Path:
        path = self.dir / name
        path.write_text(json.dumps({"context": {}, "benchmarks": benchmarks}), encoding="utf-8")
        return path

    def run_main(self, result: Path, baseline: Path, *extra: str) -> tuple[int, str]:
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = bench_compare.main([str(result), str(baseline), *extra])
        return code, out.getvalue()

    def test_within_threshold_passes(self) -> None:
        baseline = self.write("base.json", [entry("BM_A", 100.0), entry("BM_B", 10.0)])
        result = self.write("result.json", [entry("BM_A", 120.0), entry("BM_B", 8.0)])
        code, output = self.run_main(result, baseline)
        self.assertEqual(code, 0, output)
        self.assertIn("bench_compare: OK", output)

    def test_slowdown_above_threshold_fails(self) -> None:
        baseline = self.write("base.json", [entry("BM_A", 100.0)])
        result = self.write("result.json", [entry("BM_A", 126.0)])
        code, output = self.run_main(result, baseline)
        self.assertEqual(code, 1)
        self.assertIn("FAIL", output)

    def test_custom_threshold(self) -> None:
        baseline = self.write("base.json", [entry("BM_A", 100.0)])
        result = self.write("result.json", [entry("BM_A", 126.0)])
        code, _ = self.run_main(result, baseline, "--threshold", "0.5")
        self.assertEqual(code, 0)

    def test_missing_benchmark_fails_and_new_one_is_listed(self) -> None:
        baseline = self.write("base.json", [entry("BM_A", 100.0)])
        result = self.write("result.json", [entry("BM_New", 1.0)])
        code, output = self.run_main(result, baseline)
        self.assertEqual(code, 1)
        self.assertIn("missing", output)
        self.assertIn("BM_New", output)

    def test_time_units_are_normalized(self) -> None:
        baseline = self.write("base.json", [entry("BM_A", 1.0, "s")])
        result = self.write("result.json", [entry("BM_A", 1100.0, "ms")])
        code, _ = self.run_main(result, baseline)
        self.assertEqual(code, 0)

    def test_median_aggregate_is_preferred(self) -> None:
        baseline = self.write("base.json", [entry("BM_A", 100.0)])
        result = self.write("result.json", [
            entry("BM_A", 500.0), entry("BM_A", 90.0), entry("BM_A", 95.0),
            entry("BM_A_median", 95.0, run_type="aggregate", aggregate_name="median", run_name="BM_A"),
        ])
        times = bench_compare.load_times(result)
        self.assertAlmostEqual(times["BM_A"], 95.0e6)
        code, _ = self.run_main(result, baseline)
        self.assertEqual(code, 0)

    def test_errored_runs_are_ignored(self) -> None:
        result = self.write("result.json", [entry("BM_A", 1.0, error_occurred=True)])
        self.assertEqual(bench_compare.load_times(result), {})


if __name__ == "__main__":
    unittest.main()
