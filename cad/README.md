# SmoothSensors chassis CAD

A 4-part bolted chassis, defined parametrically as FreeCAD Python scripts:

- `top_plate.py` — the top layer: UNO Q, Modulino Distance/Movement/Motors, USB-C hub, camera, microphone
- `battery_box.py` — the middle layer: an open-top tray for the battery + UBEC
- `motor_mount.py` — the bottom layer, split into two bolted halves (left/right), each carrying one motor + wheel; both halves come from the same parametric definition, mirrored about the chassis centerline
- `params.py` — every physical dimension, in one place; each is tagged CONFIRMED / ESTIMATED / PLACEHOLDER inline (see "Known gaps" below) — several remain PLACEHOLDERs pending real measurements per `PHASE1.md` scenario 1
- `common.py` — shared geometry helpers (plates, standoffs, hole patterns) used by all three part scripts
- `build_all.py` — regenerates and exports everything
- `vendor/` — reference STEP files pulled from manufacturers, used to confirm real dimensions/hole positions in `params.py` (not consumed by the build scripts directly — see "Known gaps" below)

## Running it

Requires FreeCAD (tested workflow: headless `freecadcmd`, no GUI needed). From the repo root:

```
freecadcmd cad/build_all.py
```

On Windows, if `freecadcmd` isn't on your PATH, use its full path instead, e.g.:

```
"C:\Program Files\FreeCAD 0.21\bin\freecadcmd.exe" cad\build_all.py
```

Outputs land in `cad/output/` (gitignored — regenerate rather than commit them):
- `*.FCStd` — open these in the full FreeCAD GUI to inspect the model or tweak a property (every dimension is an editable property on the part, not just a script constant)
- `*.stl` — ready to slice and print

## Updating dimensions

Edit `params.py` and re-run `build_all.py`. All four parts read from the same file, so a single corrected number (e.g. the real UNO Q board footprint) regenerates every part that depends on it consistently. The 4 corner assembly-bolt positions in particular are deliberately kept in one shared function (`params.assembly_bolt_positions()`) rather than as a separate property per part, so the three stacked layers can't drift out of physical alignment with each other.

## Known gaps

Every dimension in `params.py` is now tagged CONFIRMED / ESTIMATED / PLACEHOLDER inline. Summary:

- **CONFIRMED from vendor STEP files** (`cad/vendor/UNOQsimplified.stp`, `cad/vendor/TTMotorDualShaft.stp` — see below): UNO Q's exact 4 mounting-hole XY positions (turns out to be the classic Arduino Uno R3 hole pattern, 0.05" quirk-offset hole included), the TT motor's real shaft diameter (5.4mm D-shaft), real bracket bolt pattern (2x M3 holes 17.5mm apart, 20.6mm from shaft center — not the earlier 4-hole guess), and its overall body envelope. This also revealed the motor mounts flat-face-down with the shaft passing through the mount (`motor_mount.py`'s vertical bracket rail), not lying flat with the shaft off a plate edge as first drafted — see git history for that redesign.
- **CONFIRMED from official datasheets**: UNO Q board outline, all three Modulino boards' footprint *and* exact hole spacing (they're identical — 41 x 25.36mm, 4x Ø3.2mm holes at 32x16mm spacing), the USB-C hub's outline, wheel diameter/width.
- **ESTIMATED** (secondary-sourced — the vendor pages 403'd on direct fetch, so these came from search snippets, not the primary listing): NASTIMA battery pack size *and* mounting orientation (which face is "up" is an unmade design choice, not a researched fact), UBEC size. Verify both by measuring the physical parts before printing.
- **Still genuinely unknown**: camera and microphone dimensions (no specific model on record — measure or specify), the vertical rail's own thickness/height/length (`RAIL_*` in `params.py` — structurally reasonable first guesses, not measured or stress-checked), whether the motor's 3rd hole (on a small offset tab, not coplanar with the main 2-bolt face) is worth using too.

### Reading STEP files without a GUI

Both vendor STEP files were inspected headlessly (no FreeCAD GUI): `Part.insert()` in a `freecadcmd` script to load geometry, then either a direct query for cylindrical/circular hole features, or — when a "simplified" export turns out to have no true hole geometry (`UNOQsimplified.stp`'s holes are B-spline-bounded cutouts on the board's planar faces, not `Part::GeomCircle` edges) — a `Face.Wires` scan for inner-wire cutouts on the relevant planar face. For orientation/sanity-checking a part shaped too irregularly to reason about from bounding-box numbers alone (the TT motor), tessellate to STL and render 3 flat orthographic projections with matplotlib rather than guessing from coordinates.

One `freecadcmd` gotcha worth knowing: it does **not** pass extra command-line arguments through as the script's `sys.argv` the way a normal Python interpreter would (they're instead treated as more files to open, which fails with "Unknown extension" for non-CAD files) — pass data into a script via an environment variable instead. It also runs the script as a module named after the file, not as `__main__`, so a `if __name__ == "__main__":` guard at the bottom of a script silently never fires under `freecadcmd`; `build_all.py` now calls `main()` unconditionally for this reason.
