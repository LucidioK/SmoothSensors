"""The top plate -- the uppermost chassis layer, carrying the UNO Q board,
the three Modulino boards, the USB-C hub, camera and microphone. Bolts
down onto the battery box's standoffs below.

Component mounting holes use a generic 4-corner pattern (see
common.corner_holes) inset from each component's own footprint, since real
mounting-hole patterns for most of these boards aren't confirmed yet --
see params.py's TOP_PLATE_COMPONENTS and MOUNT_HOLE_INSET.
"""

from typing import Any
import FreeCAD as App
import common
import params as P


class TopPlate(common.ParametricPart):
    def add_properties(self, obj: Any) -> None:
        obj.addProperty("App::PropertyLength", "Length", "Chassis", "Chassis length (X)").Length = P.CHASSIS_LENGTH
        obj.addProperty("App::PropertyLength", "Width", "Chassis", "Chassis width (Y)").Width = P.CHASSIS_WIDTH
        obj.addProperty("App::PropertyLength", "Thickness", "Chassis", "Plate thickness").Thickness = P.WALL_THICKNESS
        obj.addProperty("App::PropertyLength", "CornerRadius", "Chassis", "Outer corner rounding").CornerRadius = P.CORNER_RADIUS

        obj.addProperty("App::PropertyLength", "BoltHoleDiameter", "Assembly", "Clearance hole diameter for M3 bolts").BoltHoleDiameter = P.BOLT_DIAMETER
        # Note: the 4 corner assembly-bolt positions come from
        # params.assembly_bolt_positions(), shared with motor_mount.py and
        # battery_box.py -- not an editable property here, so all three
        # layers stay physically aligned. Change ASSEMBLY_BOLT_INSET in
        # params.py, not per-part.

        obj.addProperty("App::PropertyLength", "MountHoleDiameter", "Components", "Generic component mounting-hole diameter").MountHoleDiameter = P.MOUNT_HOLE_DIAMETER
        obj.addProperty("App::PropertyLength", "MountHoleInset", "Components", "Generic inset of a component's own mounting holes from its footprint edges").MountHoleInset = P.MOUNT_HOLE_INSET

    def build_shape(self, obj: Any) -> Any:
        L = obj.Length.Value
        W = obj.Width.Value
        T = obj.Thickness.Value

        plate = common.rounded_plate(L, W, T, obj.CornerRadius.Value)

        assembly_positions = P.assembly_bolt_positions()
        plate = common.cut_through_holes(plate, assembly_positions, obj.BoltHoleDiameter.Value, 0, T)

        for name, (cx, cy, comp_l, comp_w) in P.TOP_PLATE_COMPONENTS.items():
            if name == "uno_q":
                # Confirmed real hole positions (vendor STEP file), not the
                # generic corner-inset approximation -- see params.py. Cut at
                # our standard M3 clearance diameter (not the board's own
                # Ø3.2mm nominal hole size) since these are pass-through
                # clearance holes in the plate for the same bolts.
                holes = [(cx + hx - comp_l / 2.0, cy + hy - comp_w / 2.0) for hx, hy in P.UNOQ_HOLE_POSITIONS]
                hole_dia = obj.MountHoleDiameter.Value
            elif name in P.MODULINO_COMPONENT_NAMES:
                # Confirmed real hole spacing (official Arduino datasheet),
                # not the generic corner-inset approximation.
                sx, sy = P.MODULINO_HOLE_SPACING
                holes = common.spaced_holes(cx, cy, sx, sy)
                hole_dia = obj.MountHoleDiameter.Value
            else:
                local_holes = common.corner_holes(comp_l, comp_w, obj.MountHoleInset.Value)
                holes = [(cx + hx - comp_l / 2.0, cy + hy - comp_w / 2.0) for hx, hy in local_holes]
                hole_dia = obj.MountHoleDiameter.Value
            plate = common.cut_through_holes(plate, holes, hole_dia, 0, T)

        return plate


def build(doc: App.Document) -> Any:
    obj = doc.addObject("Part::FeaturePython", "TopPlate")
    TopPlate(obj)
    return obj
