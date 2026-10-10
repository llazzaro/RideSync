# Tracer 900 (2018): integrated GPS bracket and RideSync enclosure

**Design draft, not a qualified motorcycle mounting part.** The bracket itself
is included in the geometry; no purchased GPS bracket is needed for this design
concept. The main STL combines two swept mounting legs, a 94 mm crosspiece and
an electronics enclosure. The removable lid is a second STL. The navigation
ball is a hardware reference in the preview/GLB, not a printed ball supplied by
the main STL. Its bolt opening is provisional.

## Dimensions and evidence

All parameters and STL coordinates use millimetres. GLB coordinates use metres.

- Motorcycle: owner's Yamaha Tracer 900, 2018.
- Bracket crosspiece width: 94 mm, explicitly labeled in the owner's supplied
  product screenshot. This does **not** establish pivot-hole spacing.
- Board bounding envelope: 111 × 34 × 19 mm from the
  [LILYGO guide](https://wiki.lilygo.cc/products/t-sim-series/t-a7670/).
  The fitted unit is recorded as T-A7670E R2 / V1.4 in `docs/hardware.md`.
- [Official LILYGO DXF](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/dimensions/esp32/T-A7670X-ESP32.dxf)
  gives approximately 110.54 × 33.07 mm PCB outline. Its hole annotations differ
  from the guide; this draft therefore does not invent PCB fastening posts.
- [Official LILYGO STEP reference](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/dimensions/esp32/T-A7670X-Board-3D.stp).
- The leg profile is an original approximation of the supplied photos, not a
  tracing of measured fit geometry. Leg drop (80), sweep (40), pivot center
  (y=30, z=-68), pivot-hole diameter (5.5), ball diameter (25.4), ball bolt hole
  (6.5), and all wall/fastener/cable details are **design placeholders**.
- The enclosure is 134 × 50 × 34 mm plus a 3 mm lid. It spans beyond the 94 mm
  bracket to accommodate the 111 mm board envelope and lid screw bosses.
  The entire assembly has a larger depth and height than the enclosure alone.

## Generate and inspect

Create a separate Python environment and install `requirements-cad.txt`, then
run `python generate.py`. Edit `parameters.json` to regenerate the geometry.

- `output/bracket_enclosure_DRAFT.stl`: integrated bracket/body.
- `output/lid_DRAFT.stl`: removable lid; four provisional M3 clearance holes.
- `output/assembly_DRAFT.glb`: colored assembly with board/ball references.
- `output/assembly_DRAFT.png`: two-view preview.
- `output/verification.json`: mesh and clearance checks.

Exports check manifold status, watertightness, winding, positive volume, one
connected solid per part, and absence of board-envelope/body/lid intersections.
STLs are translated to z=0 without choosing a production print orientation.
These checks establish digital geometry only. The board reference is a bounding
box, not a component-level model; cable plugs and external antennas are omitted.

## Required to turn this draft into a fitted part

Measure the motorcycle's two windscreen-mechanism attachment interfaces:
outside span, pivot centers relative to the top contact surface, hole/thread
diameters, contact profile and available fastener engagement. The bracket
needs those values even though the owner does not own an existing bracket.
Check windscreen travel, instrument visibility, steering movement and cable
clearance with an unloaded fit coupon before making the full part.

Board retention is unresolved: add appropriate supports after confirming the
actual PCB underside, battery holder, components and mounting-hole dimensions.
USB plug access, battery retention, GNSS/LTE antenna placement, any future IMU,
power converter, button/LED cables and sealing hardware need their own measured
envelopes. The draft cable openings are pilots, not qualified glands. The lid
has an alignment lip, **no gasket groove**, and no water-resistance rating.

Select material, print orientation, fasteners and navigation ball hardware only
after setting load/temperature requirements. Verify the printed attachment and
fastener retention under declared bench loads before road use. No structural,
vibration, heat, weather or motorcycle-fit qualification is claimed; issue #30
remains open under `docs/acceptance_policy.md`.
