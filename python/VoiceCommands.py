import json
import os
from typing import Optional

import pyaudio
from vosk import KaldiRecognizer, Model

MODEL_PATH = os.environ.get("VOSK_MODEL_PATH", os.path.join(os.path.dirname(__file__), "model"))
SAMPLE_RATE = 16000
CHUNK_SIZE = 4000

WAKE_WORD = "robot"

COMMANDS = {
    "go ahead": "go_ahead",
    "turn right": "turn_right",
    "turn left": "turn_left",
    "stop": "stop",
}

GRAMMAR = json.dumps([f"{WAKE_WORD} {phrase}" for phrase in COMMANDS] + ["[unk]"])


class VoiceCommands:
    """Listens on the default microphone and recognizes wake-word-prefixed voice commands."""

    def __init__(self) -> None:
        model = Model(MODEL_PATH)
        self._recognizer = KaldiRecognizer(model, SAMPLE_RATE, GRAMMAR)
        self._audio = pyaudio.PyAudio()
        self._stream = self._audio.open(
            format=pyaudio.paInt16,
            channels=1,
            rate=SAMPLE_RATE,
            input=True,
            frames_per_buffer=CHUNK_SIZE,
        )
        self._stream.start_stream()

    def poll(self) -> Optional[str]:
        """Reads one chunk of audio and returns a recognized command, or None."""
        data = self._stream.read(CHUNK_SIZE, exception_on_overflow=False)
        if not self._recognizer.AcceptWaveform(data):
            return None
        text = json.loads(self._recognizer.Result()).get("text", "")
        return self._parse_command(text)

    def _parse_command(self, text: str) -> Optional[str]:
        prefix = WAKE_WORD + " "
        if not text.startswith(prefix):
            return None
        return COMMANDS.get(text[len(prefix):])

    def close(self) -> None:
        self._stream.stop_stream()
        self._stream.close()
        self._audio.terminate()
