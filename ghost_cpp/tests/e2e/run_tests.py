"""
Master Test Runner for GhostAI Resilient Network E2E Test Suite.

Executes tests across:
- Tier 1: Feature Coverage (70 tests)
- Tier 2: Boundary & Corner Cases (70 tests)
- Tier 3: Cross-Feature Combinations (20 tests)
- Tier 4: Real-World Scenarios (10 tests)
Total: 170 comprehensive E2E tests.

Outputs structured JSON report and human-readable terminal summary.
"""

import sys
import os
import time
import json
import argparse
import unittest
from io import StringIO

# Ensure current directory is in sys.path
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from test_tier1_features import TestTier1FeatureCoverage
from test_tier2_boundaries import TestTier2BoundaryCoverage
from test_tier3_combinations import TestTier3CrossFeatureCombinations
from test_tier4_scenarios import TestTier4RealWorldScenarios


class StructuredTestResult(unittest.TextTestResult):
    def __init__(self, stream, descriptions, verbosity):
        super().__init__(stream, descriptions, verbosity)
        self.test_records = []
        self._test_start_time = None

    def startTest(self, test):
        super().startTest(test)
        self._test_start_time = time.time()

    def addSuccess(self, test):
        super().addSuccess(test)
        duration = time.time() - (self._test_start_time or time.time())
        self.test_records.append({
            "test": test.id(),
            "status": "PASS",
            "duration_sec": round(duration, 4),
            "error": None
        })

    def addFailure(self, test, err):
        super().addFailure(test, err)
        duration = time.time() - (self._test_start_time or time.time())
        self.test_records.append({
            "test": test.id(),
            "status": "FAIL",
            "duration_sec": round(duration, 4),
            "error": self._exc_info_to_string(err, test)
        })

    def addError(self, test, err):
        super().addError(test, err)
        duration = time.time() - (self._test_start_time or time.time())
        self.test_records.append({
            "test": test.id(),
            "status": "ERROR",
            "duration_sec": round(duration, 4),
            "error": self._exc_info_to_string(err, test)
        })


def run_e2e_suite(selected_tier: str = "all", verbose: bool = False, report_file: str = None) -> int:
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()

    tier_map = {
        "1": [TestTier1FeatureCoverage],
        "2": [TestTier2BoundaryCoverage],
        "3": [TestTier3CrossFeatureCombinations],
        "4": [TestTier4RealWorldScenarios]
    }

    if selected_tier == "all":
        classes_to_run = [
            TestTier1FeatureCoverage,
            TestTier2BoundaryCoverage,
            TestTier3CrossFeatureCombinations,
            TestTier4RealWorldScenarios
        ]
    else:
        classes_to_run = tier_map.get(str(selected_tier), [])

    for cls in classes_to_run:
        suite.addTests(loader.loadTestsFromTestCase(cls))

    print("=" * 70)
    print("      GhostAI Resilient Network End-to-End (E2E) Test Suite")
    print(f"      Selected Scope: Tier {selected_tier.upper()}")
    print(f"      Total Tests in Scope: {suite.countTestCases()}")
    print("=" * 70)

    start_time = time.time()
    stream = StringIO() if not verbose else sys.stdout
    runner = unittest.TextTestRunner(
        stream=stream,
        verbosity=2 if verbose else 1,
        resultclass=StructuredTestResult
    )
    result = runner.run(suite)
    total_elapsed = time.time() - start_time

    # Calculate summaries
    total = result.testsRun
    failures = len(result.failures)
    errors = len(result.errors)
    passed = total - failures - errors

    print("\n" + "=" * 70)
    print("                         TEST RUN SUMMARY")
    print("=" * 70)
    print(f"Total Tests Run: {total}")
    print(f"Passed:         {passed} ({'100.0%' if total and passed == total else f'{passed/total*100:.1f}%'})")
    print(f"Failed:         {failures}")
    print(f"Errors:         {errors}")
    print(f"Total Duration: {total_elapsed:.2f}s")
    print("=" * 70)

    # Detailed tier breakdown
    records = getattr(result, "test_records", [])
    tier_counts = {"Tier 1": [0, 0], "Tier 2": [0, 0], "Tier 3": [0, 0], "Tier 4": [0, 0]}
    for r in records:
        test_name = r["test"]
        tier_key = "Tier 1"
        if "Tier2" in test_name: tier_key = "Tier 2"
        elif "Tier3" in test_name: tier_key = "Tier 3"
        elif "Tier4" in test_name: tier_key = "Tier 4"

        tier_counts[tier_key][0] += 1
        if r["status"] == "PASS":
            tier_counts[tier_key][1] += 1

    for t_name, (t_total, t_pass) in tier_counts.items():
        if t_total > 0:
            status_tag = "[PASS]" if t_pass == t_total else "[FAIL]"
            print(f"  {status_tag} {t_name}: {t_pass}/{t_total} passed")

    # Write structured JSON report
    report_data = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "scope": selected_tier,
        "summary": {
            "total": total,
            "passed": passed,
            "failed": failures,
            "errors": errors,
            "elapsed_seconds": round(total_elapsed, 3),
            "success": (failures == 0 and errors == 0)
        },
        "tier_breakdown": {
            k: {"total": v[0], "passed": v[1], "success": v[0] == v[1]} for k, v in tier_counts.items() if v[0] > 0
        },
        "tests": records
    }

    if not report_file:
        report_file = os.path.join(SCRIPT_DIR, "test_report.json")

    try:
        with open(report_file, "w", encoding="utf-8") as f:
            json.dump(report_data, f, indent=2)
        print(f"\nStructured JSON report generated at: {report_file}")
    except Exception as e:
        print(f"Warning: Failed to write report file: {e}")

    if not result.wasSuccessful():
        print("\n[FAILED TESTS DETAILS]:")
        for f in result.failures + result.errors:
            print(f[0])
            print(f[1])
        return 1

    print("\n>>> ALL TESTS PASSED SUCCESSFULLY (Exit Code 0) <<<")
    return 0


def main():
    parser = argparse.ArgumentParser(description="GhostAI Resilient Network E2E Test Suite Runner")
    parser.add_argument("--tier", choices=["1", "2", "3", "4", "all"], default="all",
                        help="Run specific test tier (default: all)")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Verbose console output showing each test execution")
    parser.add_argument("--report", type=str, default=None,
                        help="Output path for structured JSON test report")

    args = parser.parse_args()
    exit_code = run_e2e_suite(selected_tier=args.tier, verbose=args.verbose, report_file=args.report)
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
