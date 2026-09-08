"""
Unit tests for the SmoothSensors03 Linux-side Python app
(python/VoiceCommands.py and python/main.py).

Those two modules only import cleanly on the Uno Q board itself: VoiceCommands.py
needs the real `vosk` package and an `arecord` binary on PATH, and main.py needs the
board-provided `arduino.app_utils` package. To exercise them on a dev machine, this
file stubs all three (fake `vosk` / `arduino.app_utils` modules injected into
sys.modules, and a patched `subprocess.Popen`) before importing the real source
files, then drives their classes and functions directly with controlled inputs.

Run:
    python -m unittest discover -s python/tests -v

Check line coverage (requires `pip install coverage`):
    python -m coverage run -m unittest discover -s python/tests
    python -m coverage report -m --include="python/VoiceCommands.py,python/main.py"
"""
import importlib
import json
import sys
import types
import unittest
from datetime import datetime, timedelta
from pathlib import Path
from unittest import mock

PYTHON_DIR = Path(__file__).resolve().parents[1]
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

# Populated by setUpModule() once the fakes are in place and the real modules
# under test have been imported.
voice_commands_module = None
VoiceCommandsClass = None
main_module = None
fake_app_utils = None
_popen_patcher = None


def _install_fake_vosk():
    """Stand in for the real `vosk` package (not installed off-board)."""
    fake_vosk = types.ModuleType("vosk")
    fake_vosk.Model = mock.MagicMock(name="Model")
    fake_vosk.Model.side_effect = lambda *a, **k: mock.MagicMock(name="ModelInstance")
    fake_vosk.KaldiRecognizer = mock.MagicMock(name="KaldiRecognizer")
    fake_vosk.KaldiRecognizer.side_effect = lambda *a, **k: mock.MagicMock(name="RecognizerInstance")
    sys.modules["vosk"] = fake_vosk


def _install_fake_arduino_app_utils():
    """Stand in for the board-only `arduino.app_utils` package."""
    global fake_app_utils
    fake_arduino = types.ModuleType("arduino")
    fake_app_utils = types.ModuleType("arduino.app_utils")
    fake_app_utils.App = mock.MagicMock(name="App")
    fake_app_utils.Bridge = mock.MagicMock(name="Bridge")
    fake_arduino.app_utils = fake_app_utils
    sys.modules["arduino"] = fake_arduino
    sys.modules["arduino.app_utils"] = fake_app_utils


def setUpModule():
    global _popen_patcher, voice_commands_module, VoiceCommandsClass, main_module

    _install_fake_vosk()
    _install_fake_arduino_app_utils()

    # VoiceCommands.__init__ spawns `arecord` via subprocess.Popen; there's no such
    # binary (or microphone) on a dev machine, so replace it with a mock process
    # that hands out a fresh MagicMock per instantiation.
    _popen_patcher = mock.patch("subprocess.Popen")
    fake_popen = _popen_patcher.start()
    fake_popen.side_effect = lambda *a, **k: mock.MagicMock(name="ProcessInstance")

    sys.modules.pop("VoiceCommands", None)
    sys.modules.pop("main", None)

    voice_commands_module = importlib.import_module("VoiceCommands")
    VoiceCommandsClass = voice_commands_module.VoiceCommands

    # Importing main.py instantiates MainClass() and calls App.run(user_loop=loop)
    # at module scope -- both are safe no-ops now that App/Bridge/VoiceCommands'
    # dependencies are mocked.
    main_module = importlib.import_module("main")


def tearDownModule():
    _popen_patcher.stop()
    for name in ("VoiceCommands", "main", "arduino", "arduino.app_utils", "vosk"):
        sys.modules.pop(name, None)


class VoiceCommandsTests(unittest.TestCase):
    def setUp(self):
        self.instance = VoiceCommandsClass()

    def test_commands_mapping_is_exactly_the_five_move_commands(self):
        self.assertEqual(
            voice_commands_module.COMMANDS,
            {
                "go ahead": "go_ahead",
                "go back": "go_back",
                "turn right": "turn_right",
                "turn left": "turn_left",
                "stop": "stop",
            },
        )

    def test_grammar_prefixes_every_command_with_the_wake_word_plus_unk(self):
        grammar = json.loads(voice_commands_module.GRAMMAR)
        self.assertIn("[unk]", grammar)
        for phrase in voice_commands_module.COMMANDS:
            self.assertIn(f"robot {phrase}", grammar)
        self.assertEqual(len(grammar), len(voice_commands_module.COMMANDS) + 1)

    def test_arecord_command_targets_the_configured_device_and_format(self):
        cmd = voice_commands_module.ARECORD_COMMAND
        self.assertEqual(cmd[0], "arecord")
        self.assertIn(voice_commands_module.ARECORD_DEVICE, cmd)
        self.assertIn(str(voice_commands_module.SAMPLE_RATE), cmd)
        self.assertIn("raw", cmd)

    def test_init_creates_model_recognizer_and_capture_process(self):
        fake_vosk = sys.modules["vosk"]
        fake_vosk.Model.assert_called_with(voice_commands_module.MODEL_PATH)
        fake_vosk.KaldiRecognizer.assert_called_with(
            mock.ANY, voice_commands_module.SAMPLE_RATE, voice_commands_module.GRAMMAR
        )
        self.assertIsNotNone(self.instance._recognizer)
        self.assertIsNotNone(self.instance._process)

    def test_poll_returns_none_when_no_audio_available(self):
        self.instance._process.stdout.read.return_value = b""
        self.assertIsNone(self.instance.poll())
        self.instance._recognizer.AcceptWaveform.assert_not_called()

    def test_poll_returns_none_while_waveform_is_incomplete(self):
        self.instance._process.stdout.read.return_value = b"\x00" * 16000
        self.instance._recognizer.AcceptWaveform.return_value = False
        self.assertIsNone(self.instance.poll())

    def test_poll_recognizes_every_wake_worded_command(self):
        for phrase, expected in voice_commands_module.COMMANDS.items():
            with self.subTest(phrase=phrase):
                self.instance._process.stdout.read.return_value = b"\x00" * 16000
                self.instance._recognizer.AcceptWaveform.return_value = True
                self.instance._recognizer.Result.return_value = json.dumps(
                    {"text": f"robot {phrase}"}
                )
                self.assertEqual(self.instance.poll(), expected)

    def test_poll_ignores_speech_without_the_wake_word(self):
        self.instance._process.stdout.read.return_value = b"\x00" * 16000
        self.instance._recognizer.AcceptWaveform.return_value = True
        self.instance._recognizer.Result.return_value = json.dumps({"text": "go ahead"})
        self.assertIsNone(self.instance.poll())

    def test_poll_ignores_unrecognized_command_after_wake_word(self):
        self.instance._process.stdout.read.return_value = b"\x00" * 16000
        self.instance._recognizer.AcceptWaveform.return_value = True
        self.instance._recognizer.Result.return_value = json.dumps({"text": "robot dance"})
        self.assertIsNone(self.instance.poll())

    def test_poll_treats_missing_text_field_as_empty(self):
        self.instance._process.stdout.read.return_value = b"\x00" * 16000
        self.instance._recognizer.AcceptWaveform.return_value = True
        self.instance._recognizer.Result.return_value = json.dumps({})
        self.assertIsNone(self.instance.poll())

    def test_parse_command_rejects_bare_wake_word_with_no_trailing_space(self):
        self.assertIsNone(self.instance._parse_command("robot"))

    def test_parse_command_accepts_exact_match(self):
        self.assertEqual(self.instance._parse_command("robot stop"), "stop")

    def test_close_terminates_and_waits_for_the_capture_process(self):
        self.instance.close()
        self.instance._process.terminate.assert_called_once_with()
        self.instance._process.wait.assert_called_once_with()


class MainModuleTests(unittest.TestCase):
    def setUp(self):
        fake_app_utils.Bridge.reset_mock()
        self.instance = main_module.MainClass()
        self.instance.voice = mock.MagicMock(name="VoiceCommands")

    def test_init_builds_the_led_code_table_and_a_voice_recognizer(self):
        self.assertEqual(
            self.instance.led_codes,
            {
                "go_ahead": "ga",
                "go_back": "gb",
                "turn_right": "tr",
                "turn_left": "tl",
                "stop": "st",
            },
        )
        self.assertIsInstance(self.instance.previous, datetime)

    def test_loop_does_nothing_when_no_command_is_recognized(self):
        self.instance.voice.poll.return_value = None
        self.instance.previous = datetime.now()
        with mock.patch("time.sleep") as fake_sleep:
            self.instance.loop()
        fake_app_utils.Bridge.call.assert_not_called()
        fake_sleep.assert_called_once_with(0.1)

    def test_loop_forwards_every_recognized_command_to_the_bridge(self):
        for command, code in self.instance.led_codes.items():
            with self.subTest(command=command):
                fake_app_utils.Bridge.reset_mock()
                self.instance.voice.poll.return_value = command
                self.instance.previous = datetime.now()
                with mock.patch("time.sleep"):
                    self.instance.loop()
                fake_app_utils.Bridge.call.assert_any_call("show_text", code)
                fake_app_utils.Bridge.call.assert_any_call("move", command)
                self.assertEqual(fake_app_utils.Bridge.call.call_count, 2)

    def test_loop_resets_heartbeat_after_ten_seconds_elapse(self):
        self.instance.voice.poll.return_value = None
        self.instance.previous = datetime.now() - timedelta(seconds=11)
        with mock.patch("time.sleep"):
            self.instance.loop()
        self.assertLess((datetime.now() - self.instance.previous).total_seconds(), 1)

    def test_loop_leaves_heartbeat_untouched_before_ten_seconds(self):
        self.instance.voice.poll.return_value = None
        recent = datetime.now() - timedelta(seconds=2)
        self.instance.previous = recent
        with mock.patch("time.sleep"):
            self.instance.loop()
        self.assertEqual(self.instance.previous, recent)

    def test_module_level_loop_delegates_to_the_singleton_instance(self):
        with mock.patch.object(main_module.main, "loop") as fake_loop:
            main_module.loop()
        fake_loop.assert_called_once_with()

    def test_app_run_was_registered_with_the_module_level_loop_on_import(self):
        fake_app_utils.App.run.assert_called_once_with(user_loop=main_module.loop)


if __name__ == "__main__":
    unittest.main()
