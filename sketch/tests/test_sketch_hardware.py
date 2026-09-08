#!/usr/bin/env python3
"""
Hardware-in-the-loop test for the sketch (MCU) side of SmoothSensors03.

Unlike python/tests/test_python_app.py (fully mocked, runs anywhere), this test
needs the REAL board: it deploys the current sketch/, python/, and app.yaml to
the Uno Q via scripts/deploy.sh (POSIX) or scripts/deploy.ps1 (Windows) -- exactly
like a normal `scripts/deploy.sh` run -- which compiles and uploads the sketch
and restarts the app, then reads back two SSH streams, one after the other (see
capture_mcu_and_app_streams's docstring for why not concurrently), and asserts
on the status lines each side prints once a second:

  - `arduino-app-cli monitor` -- the MCU's own Serial output (sketch/sketch.ino's
    `Monitor.print(...)` calls), which is where each sensor/motor class reports:
      - SmoothDistance -> " DST: <n>cm" (or "Distance NOK" if unwired)
      - SmoothMovement -> " ax=<n> ay=<n> ..." (or "Movement NOK" if unwired)
      - RobotMotors    -> " MOT: <status>"
  - `arduino-app-cli app logs <remote_dir> --follow` -- the Python app's own
    stdout (confirmed via `arduino-app-cli app logs --help`: "Show the logs of
    the Python app" -- it does NOT carry the MCU's Monitor output, the two are
    separate serial channels). Used for the Python boot banner ("smoothsensors03")
    and the "PY" heartbeat python/main.py prints every >10s, confirming the
    Python-side loop (and therefore VoiceCommands.poll()) is alive too.

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
CAPTURED_MCU_MONITOR = ""
CAPTURED_APP_LOGS = ""
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


def _start_ssh_stream(board_host: str, remote_cmd: list[str]) -> subprocess.Popen:
    return subprocess.Popen(
        ["ssh", board_host, *remote_cmd],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )


def _stop_and_collect(proc: subprocess.Popen) -> str:
    proc.kill()
    out, _ = proc.communicate()
    return out or ""


def capture_stream(board_host: str, remote_cmd: list[str], seconds: int) -> str:
    """Runs one SSH command for a fixed, client-side-timed window and returns its output."""
    proc = _start_ssh_stream(board_host, remote_cmd)
    time.sleep(seconds)
    return _stop_and_collect(proc)


def capture_mcu_and_app_streams(board_host: str, remote_dir: str, seconds: int) -> tuple[str, str]:
    """
    Captures `arduino-app-cli monitor` (MCU Serial output) then `arduino-app-cli
    app logs <remote_dir> --follow` (Python app stdout), one after the other --
    NOT concurrently: if the board is only set up for password SSH auth (see
    README.md's "key-based access" note), two simultaneous ssh processes both
    prompting for a password race for the same terminal stdin and one of them
    loses ("Permission denied, please try again."). Sequential capture avoids
    that regardless of which auth method is configured, at the cost of roughly
    doubling this function's wall-clock time.
    """
    mcu_output = capture_stream(board_host, ["arduino-app-cli", "monitor"], seconds)
    app_output = capture_stream(board_host, ["arduino-app-cli", "app", "logs", remote_dir, "--follow"], seconds)
    return mcu_output, app_output


def setUpModule():
    global CAPTURED_MCU_MONITOR, CAPTURED_APP_LOGS, RESOLVED_BOARD_HOST

    RESOLVED_BOARD_HOST = resolve_board_host(BOARD_HOST_ARG)

    if SKIP_DEPLOY:
        print("==> --skip-deploy set, testing against whatever is currently deployed")
    else:
        run_deploy(BOARD_HOST_ARG)
        print("==> Deploy finished, giving the app a moment to finish booting")
        time.sleep(3)

    print(
        f"==> Capturing {CAPTURE_SECONDS}s of `arduino-app-cli monitor` and "
        f"`arduino-app-cli app logs` from {RESOLVED_BOARD_HOST}"
    )
    CAPTURED_MCU_MONITOR, CAPTURED_APP_LOGS = capture_mcu_and_app_streams(
        RESOLVED_BOARD_HOST, REMOTE_DIR, CAPTURE_SECONDS
    )

    if not CAPTURED_MCU_MONITOR.strip():
        raise RuntimeError(
            f"No output captured over `ssh {RESOLVED_BOARD_HOST} arduino-app-cli monitor`. "
            "Is the board reachable, and is the sketch actually running?"
        )
    if not CAPTURED_APP_LOGS.strip():
        raise RuntimeError(
            "No log output captured over "
            f"`ssh {RESOLVED_BOARD_HOST} arduino-app-cli app logs {REMOTE_DIR} --follow`. "
            "Is the board reachable, and is the app actually running?"
        )


class SketchHardwareTests(unittest.TestCase):
    """Each test inspects one of the two captured streams from setUpModule
    (CAPTURED_MCU_MONITOR or CAPTURED_APP_LOGS) for evidence that a specific
    class initialized and is running on real hardware. Order doesn't matter --
    there's no per-test state.

    There's no test for sketch.ino's "SmoothSensors003..." boot banner: it's
    printed exactly once, on the sketch's first loop() iteration after boot
    (see `_alreadyShowedAppName` in sketch.ino), and `arduino-app-cli monitor`
    is a live tail with no history buffer -- by the time this script deploys,
    waits, and attaches, that one-time line has already scrolled past. The
    status-line tests below are the reliable signal instead: none of them can
    print without setup() having already completed successfully.
    """

    def test_python_app_boots(self):
        self.assertIn(
            "smoothsensors03", CAPTURED_APP_LOGS,
            "python/main.py's startup banner never appeared -- Python app may not have started",
        )

    def test_distance_sensor_status_reported(self):
        self.assertRegex(
            CAPTURED_MCU_MONITOR, r"DST:\s*-?\d+cm|Distance NOK",
            "no SmoothDistance status line -- distance.record()/getDistanceCm() may be failing",
        )

    def test_movement_sensor_status_reported(self):
        self.assertRegex(
            CAPTURED_MCU_MONITOR, r"ax=-?\d+(\.\d+)?|Movement NOK",
            "no SmoothMovement status line -- movement.record()/get() may be failing",
        )

    def test_motor_status_reported(self):
        self.assertIn(
            "MOT:", CAPTURED_MCU_MONITOR,
            "no RobotMotors status line -- robotMotors.getStatus() may be failing",
        )

    def test_python_loop_heartbeat_reported(self):
        self.assertRegex(
            CAPTURED_APP_LOGS, r"\bPY\b",
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
        help="how long to tail `arduino-app-cli monitor`/`app logs` before asserting on them "
             "(default: 25)",
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
            then tail its MCU monitor and app logs for {args.capture_seconds}s over SSH and
            assert on them.

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
