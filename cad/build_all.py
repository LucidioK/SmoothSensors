"""Entry point: regenerates all 4 chassis parts and exports them.

Run with FreeCAD's headless interpreter from the repo root:

    freecadcmd cad/build_all.py

(On Windows, if freecadcmd isn't on PATH, use its full path, e.g.
"C:\\Program Files\\FreeCAD 0.21\\bin\\freecadcmd.exe" cad\\build_all.py)

Outputs land in cad/output/: one .FCStd per part (open these in the full
FreeCAD GUI to inspect or tweak parameters interactively) and one .stl per
part (ready for slicing). Edit params.py and re-run to regenerate.
"""

from typing import Any, Callable
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import FreeCAD as App
import Mesh

import battery_box
import motor_mount
import top_plate

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "output")


def build_and_export(name: str, build_fn: Callable[..., Any], *args: Any) -> None:
    doc = App.newDocument(name)
    obj = build_fn(doc, *args)
    doc.recompute()
    if obj.Shape.isNull() or not obj.Shape.isValid():
        raise RuntimeError("%s produced an invalid shape -- check params.py for impossible dimensions" % name)
    os.makedirs(OUT_DIR, exist_ok=True)
    doc.saveAs(os.path.join(OUT_DIR, name + ".FCStd"))
    Mesh.export([obj], os.path.join(OUT_DIR, name + ".stl"))
    App.closeDocument(doc.Name)
    print("Built %s" % name)


def main() -> None:
    build_and_export("TopPlate", top_plate.build)
    build_and_export("BatteryBox", battery_box.build)
    build_and_export("MotorMountRight", motor_mount.build, "right")
    build_and_export("MotorMountLeft", motor_mount.build, "left")


# Not guarded by `if __name__ == "__main__"`: freecadcmd sets __name__ to
# the script's filename ("build_all"), not "__main__", so that guard would
# silently never fire under the documented `freecadcmd cad/build_all.py`
# invocation.
main()
