"""Shared parametric dimensions for the SmoothSensors chassis.

All physical dimensions are in millimetres (FreeCAD's default document unit).
Confidence per value is noted inline:
  CONFIRMED    -- official datasheet/drawing, or explicitly stated in SPEC.md
  ESTIMATED    -- secondary-sourced (vendor page didn't respond to a direct
                  fetch; pulled from search-result snippets instead) --
                  verify against the physical part before printing
  PLACEHOLDER  -- pure guess, not researched at all yet

See PHASE1.md scenario 1 for the dimension-gathering task this file backs.
Every part script (top_plate.py, battery_box.py, motor_mount.py) reads
exclusively from this file, so fixing a number here and re-running
build_all.py regenerates every part correctly -- that's the whole point of
doing this parametrically instead of by hand.

Coordinate convention shared by every part (see common.py):
  X: 0 .. CHASSIS_LENGTH   (front to back)
  Y: 0 .. CHASSIS_WIDTH    (left to right)
  Z: stacks upward, motor mounts at the bottom, top plate at the top.
The centerline seam between the two motor-mount halves is at Y = CHASSIS_WIDTH / 2.
"""

# ---- Global assembly ----
WALL_THICKNESS = 3.0           # mm, FDM-friendly wall/plate thickness
CORNER_RADIUS = 4.0            # mm, rounded corners on plates (0 to disable)
BOLT_DIAMETER = 3.4            # mm, clearance hole for M3 bolts
ASSEMBLY_BOLT_INSET = 8.0      # mm, inset of the 4 corner assembly bolts from the chassis footprint edges
STANDOFF_OUTER_DIAMETER = 8.0  # mm, boss diameter around each assembly bolt
STANDOFF_HEIGHT = 20.0         # mm, PLACEHOLDER -- gap between stacked layers; must clear the tallest component in that layer

MOUNT_HOLE_DIAMETER = 3.4      # mm, M3 clearance -- CONFIRMED as the right size for the 3 Modulino boards specifically (their holes are Ø3.2mm nominal); still a generic assumption for UNO Q/hub/camera/mic
MOUNT_HOLE_INSET = 4.5         # mm, generic corner-hole inset for components without a confirmed exact pattern (UNO Q, hub, camera, mic) -- PLACEHOLDER

# ---- Overall chassis footprint (drives every stacked layer) ----
CHASSIS_LENGTH = 160.0   # mm, PLACEHOLDER
CHASSIS_WIDTH = 120.0    # mm, PLACEHOLDER


from typing import List, Tuple

def assembly_bolt_positions() -> List[Tuple[float, float]]:
    """The 4 corner bolt positions shared by every stacked layer, in the
    common chassis-frame. Each part keeps only the subset that falls
    within its own footprint (see common.positions_within)."""
    i = ASSEMBLY_BOLT_INSET
    return [
        (i, i),
        (CHASSIS_LENGTH - i, i),
        (i, CHASSIS_WIDTH - i),
        (CHASSIS_LENGTH - i, CHASSIS_WIDTH - i),
    ]


# ---- Motors / wheels (TT gear motor + 69mm wheel kit, amazon.com/dp/B098Q1BCX5) ----
# Real geometry pulled from the vendor's own STEP file (TTMotorDualShaft.stp,
# see cad/README.md) -- this motor mounts flat-face-down, shaft passing
# through a clearance hole in the mounting surface, NOT lying flat with the
# shaft hanging off a plate edge as earlier PLACEHOLDER values assumed. See
# motor_mount.py: the mount is a vertical bracket rail, not a pocketed plate.
MOTOR_MOUNT_THICKNESS = 5.0     # mm, PLACEHOLDER -- foot-plate thickness (unrelated to the rail, see RAIL_THICKNESS below)
MOTOR_SHAFT_DIAMETER = 5.4      # mm, CONFIRMED (STEP: main D-shaft diameter)
MOTOR_SHAFT_CLEARANCE = 6.0     # mm, shaft clearance-hole diameter through the rail (D-shaft 5.4mm + print tolerance)
MOTOR_BOLT_HOLE_SPACING = 17.5  # mm, CONFIRMED (STEP: the real bracket has exactly 2 structural M3 holes, straddling the shaft, NOT 4 in a rectangle as previously guessed)
MOTOR_BOLT_TO_SHAFT_OFFSET = 20.6  # mm, CONFIRMED (STEP: distance from shaft center to the bolt-hole row, along the motor's mounting face)
MOTOR_BOLT_HOLE_DIAMETER = BOLT_DIAMETER  # mm, M3 clearance (real hole is Ø3.0mm nominal; using our standard 3.4mm clearance)
MOTOR_CAN_DIAMETER = 20.0       # mm, ESTIMATED from the STEP model's motor-can cross-section -- clearance for the round motor can behind the rail
MOTOR_BODY_STANDOFF_DEPTH = 24.0   # mm, CONFIRMED-ish (STEP: mounting-face-to-body thickness ~23.3mm + margin) -- clearance needed behind the rail for the gearbox body
MOTOR_BODY_ENVELOPE_LENGTH = 55.0  # mm, CONFIRMED-ish (STEP: can+gearbox combined solid measured 53.35mm + margin) -- how far the motor body extends alongside the rail, in-plane

RAIL_THICKNESS = 5.0    # mm, PLACEHOLDER -- vertical bracket rail thickness; must be enough to hold M3 bolts securely (heat-set inserts recommended for FDM)
RAIL_MARGIN_ABOVE_SHAFT = 15.0  # mm, PLACEHOLDER -- extra rail height above the shaft hole for structural rim/edge distance

WHEEL_DIAMETER = 69.0           # mm, CONFIRMED (SPEC.md, and matches the Amazon listing title)
WHEEL_WIDTH = 29.0              # mm, CONFIRMED from the Amazon listing title -- not yet used in any cut geometry

# Shaft height above the foot's top face, DERIVED (not a free placeholder):
# chosen so the wheel's bottom edge lands level with the foot's bottom face
# (the assumed ground-contact reference plane). Revisit if the foot turns
# out not to be the chassis's lowest point once assembly is checked for
# real-world ground clearance.
RAIL_SHAFT_Z = WHEEL_DIAMETER / 2.0 - MOTOR_MOUNT_THICKNESS
RAIL_HEIGHT = RAIL_SHAFT_Z + RAIL_MARGIN_ABOVE_SHAFT  # mm, PLACEHOLDER -- derived from the above, but the margin itself is still a guess
RAIL_LENGTH_X = max(35.0, MOTOR_BOLT_HOLE_SPACING + 15.0)  # mm, rail width along the chassis-length axis, sized to comfortably clear the bolt spacing

# ---- Battery (NASTIMA 6V 6Ah LiFePO4, amazon.com/dp/B0FD3SZFXF) ----
# ESTIMATED via search snippets (vendor page 403'd on direct fetch): a
# 70 x 47 x 100mm brick, but which face is meant to sit "up" isn't stated
# anywhere -- that's a mounting *choice*, not a fact. Defaulting to lying
# the pack on its largest face (100 x 47) for the lowest chassis profile,
# leaving ~70mm as the mounted height. Revisit once the pack is in hand.
BATTERY_LENGTH = 100.0  # mm, ESTIMATED
BATTERY_WIDTH = 47.0    # mm, ESTIMATED
BATTERY_HEIGHT = 70.0   # mm, ESTIMATED, and an orientation choice on top of that

# ---- UBEC (Hobbywing 5A UBEC, High Voltage) ----
UBEC_LENGTH = 50.0   # mm, ESTIMATED (vendor page 403'd on direct fetch, pulled via search)
UBEC_WIDTH = 17.0    # mm, ESTIMATED
UBEC_HEIGHT = 10.0   # mm, ESTIMATED
BATTERY_BAY_GAP = 4.0  # mm, clearance between battery and UBEC inside the battery box

# ---- Top plate components: name -> (center_x, center_y, footprint_length, footprint_width) ----
# Layout is a first-draft PLACEHOLDER -- component positions haven't been
# checked for overlap against these updated sizes yet (in particular the
# hub at 119mm long is close to CHASSIS_LENGTH; re-check once the chassis
# footprint itself is sized for real).
UNOQ_LENGTH = 68.58  # mm, CONFIRMED -- official Arduino datasheet ABX00162-ABX00173 (docs.arduino.cc), matches the vendor STEP file (UNOQsimplified.stp) board outline to 3 decimal places
UNOQ_WIDTH = 53.34   # mm, CONFIRMED, same source

# UNOQ mounting holes: CONFIRMED exact XY positions, pulled directly from
# UNOQsimplified.stp (the board's top/bottom planar face has 4 inner-wire
# cutouts, Ø3.2mm each). Expressed as (x, y) offsets from the board's own
# corner (matching the common.corner_holes convention), 0 <= x <= UNOQ_LENGTH,
# 0 <= y <= UNOQ_WIDTH. This turns out to be exactly the classic Arduino Uno
# R3 mounting-hole pattern -- including its well-known quirk where one hole
# (the 3rd here) sits 1.27mm (0.05") off the other three's X coordinate,
# rather than a clean rectangle. Confirms UNO Q kept Uno-shield hole
# compatibility. Use this instead of the generic corner-inset pattern.
UNOQ_HOLE_DIAMETER = 3.2  # mm, CONFIRMED (STEP)
UNOQ_HOLE_POSITIONS = [
    (15.24, 50.80),
    (66.04, 35.56),
    (13.97, 2.54),
    (66.04, 7.62),
]

# All three Modulino boards (Motors, Distance, Movement) share one
# identical footprint and hole pattern -- CONFIRMED, official Arduino
# datasheets ABX00114 / ABX00102 / ABX00101, all citing the same 7.4
# mechanical section: 41 x 25.36mm PCB, 4x Ø3.2mm holes spaced 32mm
# (long axis) x 16mm (short axis), centered on the board.
MODULINO_LENGTH = 41.0    # mm, CONFIRMED
MODULINO_WIDTH = 25.36    # mm, CONFIRMED
MODULINO_HOLE_SPACING = (32.0, 16.0)  # mm, CONFIRMED (see common.spaced_holes)

# Modulino Distance's ToF sensor points out of the PCB's component face
# (perpendicular to the board), not off an edge -- so it needs to be
# mounted on-edge (vertically), component face aimed in the travel
# direction, not laid flat like the other two Modulino boards. This is a
# reading of the datasheet's pinout photo, not a stated instruction --
# confirm against the physical part before finalizing its mount.

HUB_LENGTH = 119.0   # mm, CONFIRMED -- official Arduino datasheet TPX00241 (body only, excludes the attached 175mm cable)
HUB_WIDTH = 27.8     # mm, CONFIRMED, same source
HUB_HEIGHT = 16.0    # mm, CONFIRMED, same source -- not yet used in any geometry (no height-aware cutout/standoff for it yet)

CAMERA_LENGTH = 30.0   # mm, PLACEHOLDER -- no specific model on record; ask the user or measure directly
CAMERA_WIDTH = 30.0    # mm, PLACEHOLDER

MIC_LENGTH = 20.0   # mm, PLACEHOLDER -- no specific model on record
MIC_WIDTH = 20.0    # mm, PLACEHOLDER

TOP_PLATE_COMPONENTS = {
    # name: (center_x, center_y, footprint_length, footprint_width)
    "uno_q": (CHASSIS_LENGTH * 0.35, CHASSIS_WIDTH * 0.5, UNOQ_LENGTH, UNOQ_WIDTH),
    "modulino_distance": (CHASSIS_LENGTH - 15.0, CHASSIS_WIDTH * 0.5, MODULINO_LENGTH, MODULINO_WIDTH),  # front edge, forward-facing
    "modulino_movement": (CHASSIS_LENGTH * 0.6, CHASSIS_WIDTH * 0.2, MODULINO_LENGTH, MODULINO_WIDTH),
    "modulino_motors": (CHASSIS_LENGTH * 0.6, CHASSIS_WIDTH * 0.8, MODULINO_LENGTH, MODULINO_WIDTH),
    "usb_hub": (CHASSIS_LENGTH * 0.15, CHASSIS_WIDTH * 0.5, HUB_LENGTH, HUB_WIDTH),
    "camera": (CHASSIS_LENGTH - 10.0, CHASSIS_WIDTH * 0.25, CAMERA_LENGTH, CAMERA_WIDTH),  # front edge, forward-facing
    "microphone": (CHASSIS_LENGTH - 10.0, CHASSIS_WIDTH * 0.75, MIC_LENGTH, MIC_WIDTH),
}

# Components using the CONFIRMED Modulino hole spacing instead of the
# generic corner-inset pattern -- see top_plate.py.
MODULINO_COMPONENT_NAMES = {"modulino_distance", "modulino_movement", "modulino_motors"}
