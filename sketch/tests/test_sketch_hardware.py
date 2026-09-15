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


class HardwareTestSession:
    """
    Encapsulates a single hardware test session, including the board host,
    capture duration, and whether to skip the deploy step. Provides methods to
    run the deploy script, capture logs from the board, and store the captured
    output for later assertions.
    """
    def __init__(
        self,
        board_host_arg: str | None,
        capture_seconds: int,
        skip_deploy: bool,
    ):
        """
        Initialize a HardwareTestSession with the given parameters.
        Args:
            board_host_arg (str | None): The board host argument, e.g., "arduino@10.0.0.195"
            capture_seconds (int): The duration in seconds to capture logs for
            skip_deploy (bool): Whether to skip the deploy step
        """
        self.board_host_arg = board_host_arg
        self.capture_seconds = capture_seconds
        self.skip_deploy = skip_deploy
        self.board_host = self.resolve_board_host(board_host_arg)
        self.mcu_monitor = ""
        self.app_logs = ""

    @staticmethod
    def resolve_board_host(explicit: str | None) -> str:
        """Mirrors the BOARD_HOST/BOARD_IP defaulting logic."""
        if explicit:
            return explicit
        if os.environ.get("BOARD_HOST"):
            return os.environ["BOARD_HOST"]
        return f"arduino@{os.environ.get('BOARD_IP', '10.0.0.195')}"

    def run_deploy(self) -> None:
        """Runs the platform-appropriate deploy script."""
        if platform.system() == "Windows":
            cmd = [
                "powershell",
                "-NoProfile",
                "-File",
                str(REPO_ROOT / "scripts" / "deploy.ps1"),
            ]
        else:
            cmd = ["bash", str(REPO_ROOT / "scripts" / "deploy.sh")]

        if self.board_host_arg:
            cmd.append(self.board_host_arg)

        print(f"==> Running: {' '.join(cmd)}")
        result = subprocess.run(cmd, cwd=REPO_ROOT, timeout=600, check=True)
        if result.returncode != 0:
            raise RuntimeError(
                f"deploy script exited with status {result.returncode}"
            )

    @staticmethod
    def _start_ssh_stream(
        board_host: str,
        remote_cmd: list[str],
    ) -> subprocess.Popen:
        return subprocess.Popen(
            ["ssh", board_host, *remote_cmd],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )

    @staticmethod
    def _stop_and_collect(proc: subprocess.Popen) -> str:
        proc.kill()
        out, _ = proc.communicate()
        return out or ""

    @classmethod
    def _capture_stream(
        cls,
        board_host: str,
        remote_cmd: list[str],
        seconds: int,
    ) -> str:
        proc = cls._start_ssh_stream(board_host, remote_cmd)
        time.sleep(seconds)
        return cls._stop_and_collect(proc)

    def capture(self) -> None:
        """
        Capture the MCU monitor and Python app logs from the board.
        This method will either deploy the sketch and app to the board (if skip_deploy is False)
        or use whatever is currently deployed (if skip_deploy is True).
        It uses SSH to run `arduino-app-cli monitor` and `arduino-app-cli app logs` on the board,
        capturing the output for the specified duration (capture_seconds).
        """
        if self.skip_deploy:
            print("==> --skip-deploy set, testing against whatever is currently deployed")
        else:
            self.run_deploy()
            print("==> Deploy finished, giving the app a moment to finish booting")
            time.sleep(3)

        print(
            f"==> Capturing {self.capture_seconds}s of "
            f"`arduino-app-cli monitor` and `arduino-app-cli app logs` "
            f"from {self.board_host}"
        )

        self.mcu_monitor = self._capture_stream(
            self.board_host,
            ["arduino-app-cli", "monitor"],
            self.capture_seconds,
        )
        self.app_logs = self._capture_stream(
            self.board_host,
            [
                "arduino-app-cli",
                "app",
                "logs",
                REMOTE_DIR,
                "--follow",
            ],
            self.capture_seconds,
        )

        print("\n\n=== Captured `arduino-app-cli monitor` (MCU Serial output) ===")
        print(self.mcu_monitor)
        print("\n\n=== Captured `arduino-app-cli app logs` (Python app stdout) ===")
        print(self.app_logs)

        if not self.mcu_monitor.strip():
            raise RuntimeError(
                f"No output captured over "
                f"`ssh {self.board_host} arduino-app-cli monitor`."
            )

        if not self.app_logs.strip():
            raise RuntimeError(
                "No log output captured over "
                f"`ssh {self.board_host} arduino-app-cli app logs "
                f"{REMOTE_DIR} --follow`."
            )


class SketchHardwareTests(unittest.TestCase):
    """
    TestCase for hardware-in-the-loop testing of the sketch and Python app.
    """
    session: HardwareTestSession

    @classmethod
    def configure(
        cls,
        board_host_arg: str | None,
        capture_seconds: int,
        skip_deploy: bool,
    ) -> None:
        """
        Configure the hardware test session.
        
        This must be called before setUpClass() is invoked, which is normally
        done by unittest.main() after the test class is defined. It sets up the
        HardwareTestSession with the provided parameters, which will be used to
        capture the MCU monitor and Python app logs during the tests.
        
        Args:
            board_host_arg (str | None): The board host argument, e.g., 
                "arduino@10.0.0.195".
                If None, it will be resolved from environment variables.
            capture_seconds (int): The number of seconds to capture logs from
                the board.
            skip_deploy (bool): If True, the deploy step will be skipped, and
                the tests will run against whatever is currently deployed on the
                board. If False, the sketch will be deployed before capturing logs.
        
        """
        cls.session = HardwareTestSession(
            board_host_arg,
            capture_seconds,
            skip_deploy,
        )

    @classmethod
    def setUpClass(cls) -> None:
        """
        Set up the test class by capturing logs from the hardware.
        """
        cls.session.capture()

    def test_python_app_boots(self):
        """
        Test that the Python app's startup banner appears in the logs, indicating
        that the app has booted successfully.
        """
        self.assertIn(
            "smoothsensors03",
            self.session.app_logs,
            "python/main.py's startup banner never appeared",
        )

    def test_distance_sensor_status_reported(self):
        """
        Test that the distance sensor reports its status in the MCU monitor output.
        This checks for either a valid distance reading (e.g., "DST: 123cm") or an
        error message ("Distance NOK") if the sensor is not wired correctly.
        """
        self.assertRegex(self.session.mcu_monitor, r"DST:\s*-?\d+cm|Distance NOK")

    def test_movement_sensor_status_reported(self):
        """
        Test that the movement sensor reports its status in the MCU monitor output.
        This checks for either valid acceleration readings (e.g., "ax=123 ay=456") or an
        error message ("Movement NOK") if the sensor is not wired correctly.
        """
        self.assertRegex(self.session.mcu_monitor, r"ax=\s*-?\d+(\.\d+)?|Movement NOK")

    def test_motor_status_reported(self):
        """
        Test that the motor reports its status in the MCU monitor output.
        """
        self.assertIn("MOT:", self.session.mcu_monitor)

    def test_compass_status_reported(self):
        """
        Test that the compass reports its status in the MCU monitor output.
        """
        self.assertIn("CMP:", self.session.mcu_monitor)

    def test_python_loop_heartbeat_reported(self):
        """
        Test that the Python app reports a heartbeat in the logs.
        """
        self.assertRegex(self.session.app_logs, r"\bPY\b")


def main() -> int:
    """
    Main entry point for the hardware test script.
    Returns 0 on success, non-zero on failure.
    """
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
            This would {action} a real Uno Q board at
            {HardwareTestSession.resolve_board_host(args.board_host)!r},
            then tail its MCU monitor and app logs for
            {args.capture_seconds}s over SSH and assert on them.

            Nothing was done. Re-run with --yes to actually do this.
        """))
        return 0

    SketchHardwareTests.configure(
        board_host_arg=args.board_host,
        capture_seconds=args.capture_seconds,
        skip_deploy=args.skip_deploy,
    )

    sys.argv = [sys.argv[0]] + remaining
    unittest.main()
    return 0


if __name__ == "__main__":
    sys.exit(main())
