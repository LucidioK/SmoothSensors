"""Reusable FreeCAD geometry helpers shared by every chassis part.

These are dimension-agnostic -- none of the numbers here are guesses, only
the geometric building blocks (plates, standoffs, hole patterns) that the
part scripts assemble using the real parameters from params.py.
"""

from typing import Any, List, Tuple
import FreeCAD as App
import Part


class ParametricPart:
    """Base Proxy for a Part::FeaturePython object whose Shape is rebuilt
    from its own properties on every recompute. Subclasses implement
    add_properties(obj) to declare tunable properties and build_shape(obj)
    to return the resulting Part.Shape. Because the properties live on the
    document object itself, they're editable from FreeCAD's GUI property
    editor after generation, and the shape recomputes automatically.
    """

    def __init__(self, obj: Any) -> None:
        obj.Proxy = self
        self.add_properties(obj)

    def add_properties(self, obj: Any) -> None:
        raise NotImplementedError

    def build_shape(self, obj: Any) -> Any:
        raise NotImplementedError

    def execute(self, obj: Any) -> None:
        obj.Shape = self.build_shape(obj)


def _is_vertical_edge(edge: Any, height: float, tol: float = 1e-3) -> bool:
    z0 = edge.Vertexes[0].Point.z
    z1 = edge.Vertexes[1].Point.z
    return abs(abs(z1 - z0) - height) < tol


def rounded_plate(length: float, width: float, height: float, radius: float) -> Any:
    """An axis-aligned box from (0,0,0) to (length,width,height), with its
    4 vertical edges filleted if radius > 0."""
    box = Part.makeBox(length, width, height)
    if radius <= 0:
        return box
    vertical = [e for e in box.Edges if _is_vertical_edge(e, height)]
    return box.makeFillet(radius, vertical)


def cut_through_holes(shape: Any, positions: List[Tuple[float, float]], diameter: float, z0: float, height: float) -> Any:
    """Cut a round through-hole at each (x, y) in positions, centered at
    that point, spanning z0 .. z0+height (with a little overrun on both
    ends so the cut is a clean through-hole regardless of face alignment)."""
    result = shape
    for x, y in positions:
        hole = Part.makeCylinder(
            diameter / 2.0, height + 2.0, App.Vector(x, y, z0 - 1.0)
        )
        result = result.cut(hole)
    return result


def standoff_bosses(positions: List[Tuple[float, float]], base_z: float, height: float, outer_diameter: float, hole_diameter: float) -> Any:
    """A fused solid of cylindrical bosses (with their bolt holes already
    cut) at each (x, y) in positions, sitting on top of base_z."""
    boss = None
    for x, y in positions:
        b = Part.makeCylinder(
            outer_diameter / 2.0, height, App.Vector(x, y, base_z)
        )
        hole = Part.makeCylinder(
            hole_diameter / 2.0, height + 2.0, App.Vector(x, y, base_z - 1.0)
        )
        b = b.cut(hole)
        boss = b if boss is None else boss.fuse(b)
    return boss


def rect_pocket(length: float, width: float, depth: float, base_z: float, center_x: float, center_y: float) -> Any:
    """A rectangular pocket (to be cut from a plate), centered at
    (center_x, center_y), depth mm deep starting at base_z."""
    return Part.makeBox(
        length,
        width,
        depth,
        App.Vector(center_x - length / 2.0, center_y - width / 2.0, base_z),
    )


def corner_holes(length: float, width: float, inset: float) -> List[Tuple[float, float]]:
    """4 hole positions inset from the corners of a length x width
    rectangle whose own origin is (0, 0)."""
    return [
        (inset, inset),
        (length - inset, inset),
        (inset, width - inset),
        (length - inset, width - inset),
    ]


def spaced_holes(center_x: float, center_y: float, spacing_x: float, spacing_y: float) -> List[Tuple[float, float]]:
    """4 hole positions offset by +/-spacing_x/2 and +/-spacing_y/2 from a
    center point -- matches how most datasheets actually specify a board's
    mounting-hole pattern (hole-to-hole spacing), as opposed to
    corner_holes' edge-inset convention. Use this whenever a component's
    real hole spacing is known (see params.MODULINO_HOLE_SPACING)."""
    hx, hy = spacing_x / 2.0, spacing_y / 2.0
    return [
        (center_x - hx, center_y - hy),
        (center_x + hx, center_y - hy),
        (center_x - hx, center_y + hy),
        (center_x + hx, center_y + hy),
    ]


def positions_within(positions: List[Tuple[float, float]], x0: float, x1: float, y0: float, y1: float) -> List[Tuple[float, float]]:
    """Filter (x, y) tuples to those inside the given bounding box
    (inclusive) -- used to pick out which of the 4 whole-chassis assembly
    bolt positions fall within a given part's own footprint."""
    return [(x, y) for x, y in positions if x0 <= x <= x1 and y0 <= y <= y1]
