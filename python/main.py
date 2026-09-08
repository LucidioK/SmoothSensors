"""
This is the main entry point for the SmoothSensors03 app.
It is called by the Arduino App framework.
"""
import time
from datetime import datetime
from arduino.app_utils import App, Bridge
from VoiceCommands import VoiceCommands

class MainClass:
    """
    This class is instantiated once by the App framework.
    It is responsible for initializing the app and running the main loop.
    It is not called directly by the framework, but rather by the `loop` function below.
    It is a good place to put state that needs to be shared between the loop and other functions.
    It creates a VoiceCommands instance and polls it for recognized commands, which it then
    sends to the Arduino via the Bridge.
    """
    def __init__(self):
        """
        Initializer for MainClass.
        """
        print("\n\nsmoothsensors03\n\n")
        self._previous: datetime = datetime.now()
        self._voice = VoiceCommands()
        self._led_codes = {
            "go_ahead":   "ga",
            "go_back":    "gb",
            "turn_right": "tr",
            "turn_left":  "tl",
            "stop":       "st",
        }

    def loop(self) -> None:
        """
        This function is called repeatedly by the App framework.
        It polls the VoiceCommands instance for recognized commands and sends them
        to the Arduino via the Bridge.
        It also prints a message every 10 seconds to show that the loop is running.
        """
        command = self._voice.poll()
        if command is not None:
            print(f"{datetime.now()} Voice command recognized: {command}")
            Bridge.call("show_text", self._led_codes[command])
            Bridge.call("move", command)
        if (datetime.now() - self._previous).seconds > 10:
            print(f"{datetime.now()} PY")
            self._previous = datetime.now()
        time.sleep(0.1)

main = MainClass()

def loop() -> None:
    """This function is called repeatedly by the App framework."""
    # `previous` is module-level state updated by this callback.
    main.loop()

# See: https://docs.arduino.cc/software/app-lab/tutorials/getting-started/#app-run
App.run(user_loop=loop)
