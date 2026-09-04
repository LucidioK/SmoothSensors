import json
import os
import subprocess
from typing import final

import rnnoise
import numpy as np
from vosk import KaldiRecognizer, Model

MODEL_PATH = os.environ.get("VOSK_MODEL_PATH", os.path.join(os.path.dirname(__file__), "model"))
SAMPLE_RATE = 16000
CHUNK_FRAMES = 8000
BYTES_PER_FRAME = 2  # 16-bit mono samples

# ALSA's "default" device stays pinned to the board's built-in codec (card 0),
# not the USB mic -- so the capture device must be named explicitly.
ARECORD_DEVICE = os.environ.get("ARECORD_DEVICE", "plughw:CARD=Device,DEV=0")

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
    "-D", ARECORD_DEVICE,
    "-f", "S16_LE",
    "-r", str(SAMPLE_RATE),
    "-c", "1",
    "-t", "raw",
]

@final
class VoiceCommands:
    """Captures microphone audio via arecord and recognizes wake-word-prefixed voice commands."""

    def __init__(self) -> None:
        model = Model(MODEL_PATH)
        self._recognizer = KaldiRecognizer(model, SAMPLE_RATE, GRAMMAR)
        self._denoiser = rnnoise.RNNoise()
        self._process = subprocess.Popen(ARECORD_COMMAND, stdout=subprocess.PIPE)

    def poll(self) -> str | None:
        """Reads one chunk of audio and returns a recognized command, or None."""
        data = self._process.stdout.read(CHUNK_FRAMES * BYTES_PER_FRAME)
        if not data:
            return None
        audio = np.frombuffer(data, dtype=np.int16)
        cleaned = self._denoiser.filter(audio)
        cleaned_bytes = cleaned.astype(np.int16).tobytes()
        if not self._recognizer.AcceptWaveform(cleaned_bytes):
            print("Could not accept wave form...")
            return None
        text = json.loads(self._recognizer.Result()).get("text", "")
        return self._parse_command(text)

    def _parse_command(self, text: str) -> str | None:
        prefix = WAKE_WORD + " "
        if not text.startswith(prefix):
            return None
        return COMMANDS.get(text[len(prefix):])

    def close(self) -> None:
        """
        Closes the Voice Recognizer process.
        """
        self._process.terminate()
        self._process.wait()
