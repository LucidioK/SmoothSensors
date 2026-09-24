"""
Generates a vertical sequence of components with holes and connecting boxes based on a CSV file.

This is for robots with an existing chassis, the holes here are for mounting the Arduino and
the sensors on top of the chassis. The holes are specified in a CSV file with the following columns:

    Side: "L" or "R" for left or right side of the robot
    X: X coordinate of the hole center
    Y: Y coordinate of the hole center
    Radius: Radius of the hole

All measurements are in millimeters. 

See the example CSV file "top_plate.csv" for reference, and the file "top_plate.FCStd" for the
generated part. The generated part is a vertical sequence of cylinders with holes and connecting
boxes between them. The height of the part is specified in the code, and the external radius
multiplier is used to determine the size of the connecting boxes.

This script requires FreeCAD to be installed and available in the system PATH.

See https://www.freecad.org/downloads for installation instructions. 

It can be run from the command line using the FreeCAD command line interface. The generated part
is saved as a FreeCAD document with the freecadcmd command. The script does not destroy anything,
but it will create a new document with the same name as the CSV file.

The generated part is saved as a FreeCAD document with the same name as the CSV file.

To execute it, in Linux, Mac or Windows, run the following command in the terminal:
    freecadcmd top_plate.py
"""
import math
import pathlib
from typing import List, Optional
import pandas as pd
import numpy as np
import FreeCAD as App
import Part

# ANSI color codes for terminal output
GREEN = "\033[92m"
CYAN = "\033[96m"
YELLOW = "\033[0;33m"
RED = "\033[0;31m"
NO_COLOR = "\033[0m"

def color_print(ls: list[str], colors: Optional[List[str]] = None) -> None:
    """
    Prints a list of strings with specified colors in a rotating manner.

    Args:
        ls (list[str]): The list of strings to print.
        colors (list[str], optional): The list of ANSI color codes to use for printing.
            Defaults to [GREEN, YELLOW].

    Returns:
        None
    """
    colors = colors or [GREEN, YELLOW]
    for i, s in enumerate(ls):
        color = colors[i % len(colors)]
        print(f"{color}{s}{NO_COLOR}", end="")
    print(f"{NO_COLOR}")


class VerticalSequenceOfComponentsWithHolesAndConnectingBoxes:
    """
    Generates a vertical sequence of components with holes and connecting boxes based on a CSV file
    """
    def __init__(
        self,
        holes_file_path: pathlib.Path,
        height: float = 5.0,
        external_radius_multiplier: float = 1.5,
    ) -> None:
        """
        Initializes the generator.
        Args:
            holes_file_path (pathlib.Path): The path to the CSV file containing hole data.
            height (float): The height of the component. Defaults to 5.0mm.
            external_radius_multiplier (float, optional): A multiplier for determining the external
                radius. Defaults to 1.5mm.
        """

        self.holes_file_path = holes_file_path
        self.height = height
        self.doc = App.newDocument(holes_file_path.stem)
        self.holes = pd.read_csv(holes_file_path).to_records()
        self.left_holes = sorted(
            self.holes[self.holes["Side"] == "L"], key=lambda x: x.Y
        )
        self.right_holes = sorted(
            self.holes[self.holes["Side"] == "R"], key=lambda x: x.Y
        )
        self.min_radius = min(self.holes["Radius"])
        self.max_radius = max(self.holes["Radius"])
        self.external_radius_multiplier = external_radius_multiplier
        self.connecting_box_width = (
            self.min_radius * self.external_radius_multiplier * 2
        )
        color_print(
            [
                "Initializing generator for ",
                str(len(self.holes)),
                " holes. ",
                "Height: ",
                str(self.height),
                ", Min radius: ",
                str(self.min_radius),
                ", Max radius: ",
                str(self.max_radius),
                ", Connecting box width: ",
                str(self.connecting_box_width),
            ],
            [GREEN, YELLOW],
        )
        self.base_box = None

    def create(self) -> Part.Shape:
        """
        Creates the vertical sequence of components with holes and connecting boxes.
        Returns:
            Part.Shape: shape of the final part created.
        """
        color_print(["Drawing ", "Left", " part, ", len(self.left_holes), " holes"])
        left_part = self._draw_cylinders_with_connecting_boxes(self.left_holes)
        color_print(
            ["Drawing ", "Right", " part, ", len(self.right_holes), " holes"]
        )
        right_part = self._draw_cylinders_with_connecting_boxes(self.right_holes)
        color_print(["Drawing ", "bottom ", "rod"])
        left_part = self._draw_connecting_box_between_holes(
            left_part, self.left_holes[0], self.right_holes[0]
        )
        color_print(["Drawing ", "top ", "rod"])
        left_part = self._draw_connecting_box_between_holes(
            left_part, self.left_holes[-1], self.right_holes[-1]
        )

        color_print(["Fusing ", "Left", " and ", "Right", " parts"])
        # Fuse the left and right parts with the base box
        final_part = left_part.fuse(right_part)

        color_print(["Adding all holes "])
        final_part = self._cut_holes(final_part, self.holes)

        self.base_box = self.doc.addObject("Part::Feature", "base_box")
        self.base_box.Shape = final_part
        self.doc.ShowHidden = True
        self.doc.recompute()

        self.doc.saveAs(self.holes_file_path.stem + ".FCStd")
        return final_part

    def _cut_holes(
        self, base_part: Part.Shape, hole_list: np.rec.recarray
    ) -> Part.Shape:
        for hole in hole_list:
            color_print(
                [
                    "  Cutting hole at (",
                    str(hole.X),
                    ", ",
                    str(hole.Y),
                    ") with radius ",
                    str(hole.Radius),
                ],
                [CYAN, YELLOW],
            )
            hole_internal_shape = Part.makeCylinder(
                hole.Radius, self.height * 2, App.Vector(hole.X, hole.Y, 0)
            )
            base_part = base_part.cut(hole_internal_shape)
        return base_part

    def _draw_connecting_box_between_holes(
        self, base_part: Part.Shape, hole1: np.rec.record, hole2: np.rec.record
    ) -> Part.Shape:
        # Create a box connecting the two holes
        color_print(
            [
                "  Creating connecting box between holes at (",
                str(hole1.X),
                ", ",
                str(hole1.Y),
                ") and (",
                str(hole2.X),
                ", ",
                str(hole2.Y),
                ")",
            ],
            [CYAN, YELLOW],
        )
        box_length = math.sqrt((hole2.X - hole1.X) ** 2 + (hole2.Y - hole1.Y) ** 2)
        box_width = self.connecting_box_width
        box_height = self.height
        box_center_x = (hole1.X + hole2.X) / 2
        box_center_y = (hole1.Y + hole2.Y) / 2

        # Calculate the angle of rotation for the connecting box
        angle = math.atan2(hole2.Y - hole1.Y, hole2.X - hole1.X)

        # Create the connecting box and rotate it to align with the holes
        connecting_box = Part.makeBox(
            box_length,
            box_width,
            box_height,
            App.Vector(
                box_center_x - box_length / 2, box_center_y - box_width / 2, 0
            ),
        )
        connecting_box.rotate(
            App.Vector(box_center_x, box_center_y, 0),
            App.Vector(0, 0, 1),
            math.degrees(angle),
        )

        # Fuse the connecting box with the base part
        return base_part.fuse(connecting_box)

    def _draw_cylinders_with_connecting_boxes(
        self, hole_list: np.rec.recarray
    ) -> Part.Shape:
        previous_hole = None
        final_part = None
        for hole in hole_list:
            color_print(
                [
                    "  Processing hole at (",
                    str(hole.X),
                    ", ",
                    str(hole.Y),
                    ") with radius ",
                    str(hole.Radius),
                ],
                [CYAN, YELLOW],
            )
            hole_external_cylinder = Part.makeCylinder(
                hole.Radius * 2, self.height * 2, App.Vector(hole.X, hole.Y, 0)
            )
            if final_part is None:
                final_part = hole_external_cylinder
            else:
                final_part = self._draw_connecting_box_between_holes(
                    final_part, previous_hole, hole
                )
                final_part = final_part.fuse(hole_external_cylinder)
            previous_hole = hole

        for hole in hole_list:
            hole_internal_shape = Part.makeCylinder(
                hole.Radius, self.height * 2, App.Vector(hole.X, hole.Y, 0)
            )
            final_part = final_part.cut(hole_internal_shape)

        return final_part

csv_file_path = pathlib.Path("top_plate_holes.csv")
if not csv_file_path.exists():
    raise FileNotFoundError(f"Could not find {csv_file_path.name} in the current directory.")

generator = VerticalSequenceOfComponentsWithHolesAndConnectingBoxes(
    csv_file_path, 5, external_radius_multiplier=1.5
)
generator.create()
