#!/usr/bin/env python3
"""
Hardware-in-the-loop test for the sketch (MCU) side of SmoothSensors03.

Unlike python/tests/test_python_app.py (fully mocked, runs anywhere), this test
needs the REAL board: it deploys the current sketch/, python/, and app.yaml to
the Uno Q via scripts/deploy.sh (POSIX) or scripts/deploy.ps1 (Windows) -- exactly
like a normal `scripts/deploy.sh` run -- which compiles and uploads the sketch
and restarts the app, then reads back a window of `arduino-app-cli app logs`
over SSH and asserts on the status lines sketch/sketch.ino's main loop prints
once a second for each sensor/motor class:

  - SmoothDistance  -> " DST: <n>cm" (or "Distance NOK" if unwired)
  - SmoothMovement  -> " ax=<n> ay=<n> ..." (or "Movement NOK" if unwired)
  - RobotMotors     -> " MOT: <status>"
  - the MCU boot banner ("SmoothSensors003...") and the Python app boot banner
    ("smoothsensors03") confirm both halves of the app actually came up
  - the "PY" heartbeat that python/main.py prints every >10s confirms the
    Python-side loop (and therefore VoiceCommands.poll()) is alive too

Together these prove that after a REAL compile+upload, every sensor/motor
wrapper class initializes and runs each iteration of the main loop without
crashing it -- the best black-box signal available, since sketch.ino currently
only exposes `show_text` and `move` over RPC (no sensor-read RPC yet, see
SPEC.md's `get_sensor_values()` bridge), and those two are only ever invoked
from a recognized voice command in the real app, so there's no test-only hook
to call them directly without also faking microphone audio.

SAFETY: this deploys to and restarts a real, physically present robot. It is
NOT run automatically and is NOT part of `python -m unittest discover`; you
must invoke it directly and pass --yes.

Usage:
    python sketch/tests/test_sketch_hardware.py --yes
    python sketch/tests/test_sketch_hardware.py arduino@10.0.0.195 --yes
    BOARD_HOST=arduino@10.0.0.195 python sketch/tests/test_sketch_hardware.py --yes

    # Re-run just the log assertions against whatever is already deployed,
    # without re-flashing the board:
    python sketch/tests/test_sketch_hardware.py --yes --skip-deploy
"""
import argparse
import os
import platform
import subprocess
import sys
import time
import unittest
from pathlib import Path
from textwrap import dedent

REPO_ROOT = Path(__file__).resolve().parents[2]
APP_NAME = "smoothsensors03"
REMOTE_DIR = f"ArduinoApps/{APP_NAME}"

# Populated by main() from CLI args before unittest.main() runs, then consumed
# by setUpModule().
BOARD_HOST_ARG: str | None = None
CAPTURE_SECONDS = 25
SKIP_DEPLOY = False

# Populated by setUpModule(); read by every test method.
CAPTURED_LOGS = ""
RESOLVED_BOARD_HOST = ""


def resolve_board_host(explicit: str | None) -> str:
    """Mirrors the BOARD_HOST/BOARD_IP defaulting logic in scripts/deploy.sh and deploy.ps1."""
    if explicit:
        return explicit
    if os.environ.get("BOARD_HOST"):
        return os.environ["BOARD_HOST"]
    board_ip = os.environ.get("BOARD_IP", "10.0.0.195")
    return f"arduino@{board_ip}"


def run_deploy(board_host_arg: str | None) -> None:
    """Runs the platform-appropriate deploy script, exactly as a developer would by hand."""
    if platform.system() == "Windows":
        cmd = ["powershell", "-NoProfile", "-File", str(REPO_ROOT / "scripts" / "deploy.ps1")]
    else:
        cmd = ["bash", str(REPO_ROOT / "scripts" / "deploy.sh")]
    if board_host_arg:
        cmd.append(board_host_arg)

    print(f"==> Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, cwd=REPO_ROOT, timeout=600)
    if result.returncode != 0:
        raise RuntimeError(f"deploy script exited with status {result.returncode}")


def capture_logs(board_host: str, remote_dir: str, seconds: int) -> str:
    """
    Tails `arduino-app-cli app logs <remote_dir> --follow` over SSH for a fixed
    window, client-side timed (so it works whether or not the board has a
    `timeout` binary), and returns whatever it printed.
    """
    proc = subprocess.Popen(
        ["ssh", board_host, "arduino-app-cli", "app", "logs", remote_dir, "--follow"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    try:
        out, _ = proc.communicate(timeout=seconds)
    except subprocess.TimeoutExpired:
        proc.kill()
        out, _ = proc.communicate()
    return out or ""


def setUpModule():
    global CAPTURED_LOGS, RESOLVED_BOARD_HOST

    RESOLVED_BOARD_HOST = resolve_board_host(BOARD_HOST_ARG)

    if SKIP_DEPLOY:
        print("==> --skip-deploy set, testing against whatever is currently deployed")
    else:
        run_deploy(BOARD_HOST_ARG)
        print("==> Deploy finished, giving the app a moment to finish booting")
        time.sleep(3)

    print(f"==> Capturing {CAPTURE_SECONDS}s of `arduino-app-cli app logs` from {RESOLVED_BOARD_HOST}")
    CAPTURED_LOGS = capture_logs(RESOLVED_BOARD_HOST, REMOTE_DIR, CAPTURE_SECONDS)

    if not CAPTURED_LOGS.strip():
        raise RuntimeError(
            "No log output captured over "
            f"`ssh {RESOLVED_BOARD_HOST} arduino-app-cli app logs {REMOTE_DIR} --follow`. "
            "Is the board reachable, and is the app actually running?"
        )


class SketchHardwareTests(unittest.TestCase):
    """Each test inspects the same captured log window (see setUpModule) for one
    sensor/motor wrapper's evidence that it initialized and is running on real
    hardware. Order doesn't matter -- there's no per-test state."""

    def test_mcu_sketch_boots(self):
        self.assertIn(
            "SmoothSensors003", CAPTURED_LOGS,
            "sketch.ino's setup() banner never appeared -- MCU sketch may not have booted",
        )

    def test_python_app_boots(self):
        self.assertIn(
            "smoothsensors03", CAPTURED_LOGS,
            "python/main.py's startup banner never appeared -- Python app may not have started",
        )

    def test_distance_sensor_status_reported(self):
        self.assertRegex(
            CAPTURED_LOGS, r"DST:\s*-?\d+cm|Distance NOK",
            "no SmoothDistance status line -- distance.record()/getDistanceCm() may be failing",
        )

    def test_movement_sensor_status_reported(self):
        self.assertRegex(
            CAPTURED_LOGS, r"ax=-?\d+(\.\d+)?|Movement NOK",
            "no SmoothMovement status line -- movement.record()/get() may be failing",
        )

    def test_motor_status_reported(self):
        self.assertIn(
            "MOT:", CAPTURED_LOGS,
            "no RobotMotors status line -- robotMotors.getStatus() may be failing",
        )

    def test_python_loop_heartbeat_reported(self):
        self.assertRegex(
            CAPTURED_LOGS, r"\bPY\b",
            f"no 'PY' heartbeat seen in {CAPTURE_SECONDS}s -- python/main.py's loop() may have "
            "stalled or CAPTURE_SECONDS is too short (it prints every >10s)",
        )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Deploy the sketch to a real Uno Q and check it's running correctly.",
    )
    parser.add_argument(
        "board_host", nargs="?", default=None,
        help="board-user@host, e.g. arduino@10.0.0.195 (default: $BOARD_HOST, or "
             "arduino@$BOARD_IP, or arduino@10.0.0.195)",
    )
    parser.add_argument(
        "--capture-seconds", type=int, default=25,
        help="how long to tail `arduino-app-cli app logs` before asserting on it (default: 25)",
    )
    parser.add_argument(
        "--skip-deploy", action="store_true",
        help="skip scripts/deploy.sh|ps1 and just run the log assertions against whatever "
             "is already deployed",
    )
    parser.add_argument(
        "--yes", action="store_true",
        help="actually deploy to and query the real board; without this flag the script "
             "only explains what it would do",
    )
    args, remaining = parser.parse_known_args()

    if not args.yes:
        action = (
            "skip deploying and just read back logs from"
            if args.skip_deploy
            else "DEPLOY (compile + upload the sketch, restart the app) to"
        )
        print(dedent(f"""\
            This would {action} a real Uno Q board at {resolve_board_host(args.board_host)!r},
            then tail its logs for {args.capture_seconds}s over SSH and assert on them.

            Nothing was done. Re-run with --yes to actually do this, e.g.:
                python {Path(__file__).name} --yes
        """))
        return 0

    global BOARD_HOST_ARG, CAPTURE_SECONDS, SKIP_DEPLOY
    BOARD_HOST_ARG = args.board_host
    CAPTURE_SECONDS = args.capture_seconds
    SKIP_DEPLOY = args.skip_deploy

    # Hand the rest of argv to unittest (e.g. -v, or a specific test name).
    sys.argv = [sys.argv[0]] + remaining
    unittest.main()
    return 0


if __name__ == "__main__":
    sys.exit(main())
