#!/usr/bin/env python3
"""
Runs the full SmoothSensors03 test suite: the mocked, always-safe Python tests
(python/tests/), then the sketch hardware tests (sketch/tests/test_sketch_hardware.py).

The Python tests always run and never touch real hardware. The sketch tests are
a hardware-in-the-loop suite that deploys to and restarts a REAL Uno Q board --
see sketch/tests/test_sketch_hardware.py's docstring. They are forwarded any
extra arguments given to this script, and (like that script on its own) do
nothing but print what they would do unless you pass --yes.

Usage:
    python test.py                       # Python tests, then a safe sketch-test dry run
    python test.py --yes                 # Python tests, then a real deploy + hardware test
    python test.py --yes arduino@10.0.0.195 --skip-deploy
"""
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent


def run_python_tests() -> int:
    print("=" * 70)
    print("Running Python tests (python/tests/) ...")
    print("=" * 70)
    result = subprocess.run(
        [sys.executable, "-m", "unittest", "discover", "-s", "python/tests", "-v"],
        cwd=REPO_ROOT,
    )
    return result.returncode


def run_sketch_tests(extra_args: list[str]) -> int:
    print()
    print("=" * 70)
    print("Running sketch hardware tests (sketch/tests/test_sketch_hardware.py) ...")
    print("=" * 70)
    result = subprocess.run(
        [sys.executable, str(REPO_ROOT / "sketch" / "tests" / "test_sketch_hardware.py"), *extra_args],
        cwd=REPO_ROOT,
    )
    return result.returncode


def main() -> int:
    extra_args = sys.argv[1:]

    python_status = run_python_tests()
    sketch_status = run_sketch_tests(extra_args)

    print()
    print("=" * 70)
    print(f"Python tests:  {'PASSED' if python_status == 0 else 'FAILED'}")
    print(f"Sketch tests:  {'PASSED' if sketch_status == 0 else 'FAILED'}")
    print("=" * 70)

    return 0 if python_status == 0 and sketch_status == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
