import time
from datetime import datetime
from arduino.app_utils import App

print("Hello world lk01!")

previous: datetime = datetime.now()

def loop() -> None:
    """This function is called repeatedly by the App framework."""
    global previous
    # You can replace this with any code you want your App to run repeatedly.
    if (datetime.now() - previous).seconds > 10:
        print("PY")
        previous = datetime.now()
    time.sleep(0.1)


# See: https://docs.arduino.cc/software/app-lab/tutorials/getting-started/#app-run
App.run(user_loop=loop)
