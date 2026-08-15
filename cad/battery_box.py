"""The battery box -- the middle layer of the chassis, stacked between the
two motor mounts below and the top plate above. An open-top tray sized to
fit the battery and UBEC side by side, with standoffs bolting down to the
motor mounts and standoffs on top for the top plate.
"""

from typing import Any
import common
import params as P


class BatteryBox(common.ParametricPart):
    def add_properties(self, obj: Any) -> None:
        obj.addProperty("App::PropertyLength", "Length", "Chassis", "Chassis length (X)").Length = P.CHASSIS_LENGTH
        obj.addProperty("App::PropertyLength", "Width", "Chassis", "Chassis width (Y)").Width = P.CHASSIS_WIDTH
        obj.addProperty("App::PropertyLength", "FloorThickness", "Chassis", "Floor thickness").FloorThickness = P.WALL_THICKNESS
        obj.addProperty("App::PropertyLength", "CornerRadius", "Chassis", "Outer corner rounding").CornerRadius = P.CORNER_RADIUS

        obj.addProperty("App::PropertyLength", "BoltHoleDiameter", "Assembly", "Clearance hole diameter for M3 bolts").BoltHoleDiameter = P.BOLT_DIAMETER
        obj.addProperty("App::PropertyLength", "StandoffOuterDiameter", "Assembly", "Standoff boss diameter").StandoffOuterDiameter = P.STANDOFF_OUTER_DIAMETER
        obj.addProperty("App::PropertyLength", "StandoffHeight", "Assembly", "Gap to the top plate above").StandoffHeight = P.STANDOFF_HEIGHT
        # Note: the 4 corner assembly-bolt positions come from
        # params.assembly_bolt_positions(), shared with motor_mount.py and
        # top_plate.py -- not an editable property here, so all three
        # layers stay physically aligned. Change ASSEMBLY_BOLT_INSET in
        # params.py, not per-part.

        obj.addProperty("App::PropertyLength", "BatteryLength", "Battery", "Battery pack length").BatteryLength = P.BATTERY_LENGTH
        obj.addProperty("App::PropertyLength", "BatteryWidth", "Battery", "Battery pack width").BatteryWidth = P.BATTERY_WIDTH
        obj.addProperty("App::PropertyLength", "BatteryHeight", "Battery", "Battery pack height").BatteryHeight = P.BATTERY_HEIGHT
        obj.addProperty("App::PropertyLength", "UbecLength", "Battery", "UBEC length").UbecLength = P.UBEC_LENGTH
        obj.addProperty("App::PropertyLength", "UbecWidth", "Battery", "UBEC width").UbecWidth = P.UBEC_WIDTH
        obj.addProperty("App::PropertyLength", "UbecHeight", "Battery", "UBEC height").UbecHeight = P.UBEC_HEIGHT
        obj.addProperty("App::PropertyLength", "BayGap", "Battery", "Clearance between battery and UBEC, side by side").BayGap = P.BATTERY_BAY_GAP
        obj.addProperty("App::PropertyLength", "BayMargin", "Battery", "Extra clearance around the battery+UBEC footprint").BayMargin = 3.0

    def build_shape(self, obj: Any) -> Any:
        L = obj.Length.Value
        W = obj.Width.Value
        floor_t = obj.FloorThickness.Value

        bay_length = max(obj.BatteryLength.Value, obj.UbecLength.Value) + 2 * obj.BayMargin.Value
        bay_width = obj.BatteryWidth.Value + obj.BayGap.Value + obj.UbecWidth.Value + 2 * obj.BayMargin.Value
        bay_height = max(obj.BatteryHeight.Value, obj.UbecHeight.Value) + obj.BayMargin.Value
        box_height = floor_t + bay_height

        box = common.rounded_plate(L, W, box_height, obj.CornerRadius.Value)

        cavity = common.rect_pocket(
            bay_length, bay_width, bay_height + 1.0, floor_t, L / 2.0, W / 2.0,
        )
        box = box.cut(cavity)

        assembly_positions = P.assembly_bolt_positions()

        # Bottom clearance holes, aligned with the motor mounts' standoffs below.
        box = common.cut_through_holes(box, assembly_positions, obj.BoltHoleDiameter.Value, 0, floor_t)

        # Standoffs on top for the top plate above.
        standoffs = common.standoff_bosses(
            assembly_positions, box_height, obj.StandoffHeight.Value,
            obj.StandoffOuterDiameter.Value, obj.BoltHoleDiameter.Value,
        )
        if standoffs is not None:
            box = box.fuse(standoffs)

        return box


def build(doc: Any) -> Any:
    obj = doc.addObject("Part::FeaturePython", "BatteryBox")
    BatteryBox(obj)
    return obj
