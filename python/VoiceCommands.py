import json
import os
import subprocess
from typing import Optional

from vosk import KaldiRecognizer, Model

MODEL_PATH = os.environ.get("VOSK_MODEL_PATH", os.path.join(os.path.dirname(__file__), "model"))
SAMPLE_RATE = 16000
CHUNK_FRAMES = 4000
BYTES_PER_FRAME = 2  # 16-bit mono samples

WAKE_WORD = "robot"

COMMANDS = {
    "go ahead": "go_ahead",
    "turn right": "turn_right",
    "turn left": "turn_left",
    "stop": "stop",
}

GRAMMAR = json.dumps([f"{WAKE_WORD} {phrase}" for phrase in COMMANDS] + ["[unk]"])

ARECORD_COMMAND = [
    "arecord",
    "-q",
    "-f", "S16_LE",
    "-r", str(SAMPLE_RATE),
    "-c", "1",
    "-t", "raw",
]


class VoiceCommands:
    """Captures microphone audio via arecord and recognizes wake-word-prefixed voice commands."""

    def __init__(self) -> None:
        model = Model(MODEL_PATH)
        self._recognizer = KaldiRecognizer(model, SAMPLE_RATE, GRAMMAR)
        self._process = subprocess.Popen(ARECORD_COMMAND, stdout=subprocess.PIPE)

    def poll(self) -> Optional[str]:
        """Reads one chunk of audio and returns a recognized command, or None."""
        data = self._process.stdout.read(CHUNK_FRAMES * BYTES_PER_FRAME)
        if not data or not self._recognizer.AcceptWaveform(data):
            return None
        text = json.loads(self._recognizer.Result()).get("text", "")
        return self._parse_command(text)

    def _parse_command(self, text: str) -> Optional[str]:
        prefix = WAKE_WORD + " "
        if not text.startswith(prefix):
            return None
        return COMMANDS.get(text[len(prefix):])

    def close(self) -> None:
        self._process.terminate()
        self._process.wait()
