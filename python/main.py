import time
from datetime import datetime
from arduino.app_utils import App
from VoiceCommands import VoiceCommands

print("Hello world lk01!")

previous: datetime = datetime.now()
voice = VoiceCommands()

def loop() -> None:
    """This function is called repeatedly by the App framework."""
    global previous
    command = voice.poll()
    if command is not None:
        print(f"Voice command recognized: {command}")
    # You can replace this with any code you want your App to run repeatedly.
    if (datetime.now() - previous).seconds > 10:
        print("PY")
        previous = datetime.now()
    time.sleep(0.1)


# See: https://docs.arduino.cc/software/app-lab/tutorials/getting-started/#app-run
App.run(user_loop=loop)
