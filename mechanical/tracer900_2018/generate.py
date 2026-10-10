"""Generate a provisional integrated Tracer bracket/enclosure in millimetres.

Requires the adjacent requirements-cad.txt. Motorcycle interface parameters
are placeholders: mesh validity does not establish fit or structural strength.
"""

import json
from pathlib import Path

import manifold3d as m
import numpy as np
import trimesh

ROOT = Path(__file__).resolve().parent
P = json.loads((ROOT / "parameters.json").read_text())
OUT = ROOT / "output"
OUT.mkdir(exist_ok=True)


def box(size, center):
    return m.Manifold.cube(size, center=True).translate(center)


def cylinder(diameter, height, center, axis="z"):
    obj = m.Manifold.cylinder(height, diameter / 2, circular_segments=48, center=True)
    if axis == "x":
        obj = obj.rotate((0, 90, 0))
    elif axis == "y":
        obj = obj.rotate((90, 0, 0))
    return obj.translate(center)


def rounded_box(length, depth, height, center, radius=3):
    points = [(x, y) for x in (-length / 2 + radius, length / 2 - radius)
              for y in (-depth / 2 + radius, depth / 2 - radius)]
    solids = [cylinder(radius * 2, height, (x, y, 0)) for x, y in points]
    return m.Manifold.batch_boolean(solids, m.OpType.Add).hull().translate(center)


def mesh(solid):
    assert solid.status() == m.Error.NoError, solid.status()
    raw = solid.to_mesh()
    return trimesh.Trimesh(vertices=np.asarray(raw.vert_properties)[:, :3],
                           faces=np.asarray(raw.tri_verts), process=True)


def build():
    w, t = P["bracket_width"], P["leg_thickness"]
    d, s = P["leg_drop"], P["leg_sweep"]
    # Side silhouette inferred from photographs; not a measured bike interface.
    profile = [(-10, 12), (10, 12), (.4*s, 9), (.6*s, 0),
               (.75*s, -.19*d), (.875*s, -.525*d), (s, -.79*d),
               (.975*s, -.94*d), (.775*s, -d), (.55*s, -.91*d),
               (.475*s, -.675*d), (.35*s, -.31*d), (.225*s, -.1*d),
               (-2, -2), (-10, 0)]
    # Manifold's default positive fill requires a counterclockwise exterior.
    leg = m.CrossSection([profile[::-1]]).extrude(t).rotate((90, 0, 90))
    bracket = box((w, 20, 12), (0, 0, 6))
    for x in (-w/2, w/2-t):
        bracket += leg.translate((x, 0, 0))
    bracket -= cylinder(P["pivot_hole_diameter"], w+2,
                        (0, P["pivot_y"], P["pivot_z"]), "x")
    bracket -= cylinder(P["ball_bolt_hole_diameter"], 24, (0, 0, 6))

    length, depth, height, wall = (P[k] for k in
                                  ("case_length", "case_depth", "case_height", "wall"))
    cy, bottom = -9-depth/2, -19
    top = bottom+height
    shell = rounded_box(length, depth, height, (0, cy, bottom+height/2))
    shell -= rounded_box(length-2*wall, depth-2*wall, height+4,
                         (0, cy, bottom+wall+(height+4)/2), radius=1)
    bosses = []
    for x in (-length/2+6, length/2-6):
        for y in (cy-depth/2+6, cy+depth/2-6):
            bosses.append((x, y))
            shell += cylinder(8, height-3, (x, y, bottom+3+(height-3)/2))
    main = bracket+shell
    # Pilot cable openings: gland/grommet selection and sealing are unresolved.
    for x in (-43, 43):
        main -= cylinder(P["cable_hole_diameter"], 12, (x, cy-depth/2, -6), "y")
    for x, y in bosses:
        main -= cylinder(2.7, 15, (x, y, top-5))

    lid = rounded_box(length, depth, P["lid_thickness"],
                      (0, cy, top+P["lid_thickness"]/2))
    lip = rounded_box(length-2*wall-.6, depth-2*wall-.6, 2, (0, cy, top-1), radius=1)
    lip -= rounded_box(length-2*wall-4, depth-2*wall-4, 4, (0, cy, top-1), radius=1)
    for x, y in bosses:
        lip -= cylinder(9, 6, (x, y, top))
    lid += lip
    for x, y in bosses:
        lid -= cylinder(3.4, 12, (x, y, top))

    # Translucent, bounding-box references only; neither is a printable part.
    board = box((P["board_length"], P["board_width"], P["board_height"]),
                (0, cy, bottom+wall+3+P["board_height"]/2))
    ball = m.Manifold.sphere(P["ball_diameter_reference"]/2, circular_segments=48)
    ball = ball.translate((0, 0, 22+P["ball_diameter_reference"]/2))
    ball += cylinder(12, 12, (0, 0, 18))
    # Independent geometric clearance check for the selected board envelope.
    assert (board ^ main).volume() < 1e-6, "Board envelope intersects solid"
    assert (board ^ lid).volume() < 1e-6, "Board envelope intersects lid"
    assert (main ^ lid).volume() < 1e-6, "Lid interferes with body"
    return {"bracket_enclosure": main, "lid": lid,
            "board_envelope_reference": board, "ball_hardware_reference": ball}


def export(parts):
    scene = trimesh.Scene()
    colors = {"bracket_enclosure": [50, 61, 77, 255], "lid": [82, 105, 133, 220],
              "board_envelope_reference": [45, 190, 145, 100],
              "ball_hardware_reference": [210, 155, 58, 255]}
    report = {"status": "DESIGN DRAFT: motorcycle fit, retention, sealing and strength unverified",
              "units": "mm", "parts": {}}
    for name, solid in parts.items():
        obj = mesh(solid)
        assert obj.is_watertight and obj.is_winding_consistent and obj.volume > 0, name
        assert len(solid.decompose()) == 1, f"Disconnected geometry: {name}"
        report["parts"][name] = {"watertight": bool(obj.is_watertight),
                                  "connected_solids": len(solid.decompose()),
                                  "bounds_mm": obj.bounds.tolist(),
                                  "volume_mm3": float(obj.volume)}
        if not name.endswith("reference"):
            printable = obj.copy()
            printable.apply_translation((0, 0, -printable.bounds[0, 2]))
            printable.export(OUT / f"{name}_DRAFT.stl")
        # GLB uses metres by convention. STL and parameter dimensions use mm.
        obj.apply_scale(.001)
        obj.visual.vertex_colors = colors[name]
        scene.add_geometry(obj, geom_name=name, node_name=name)
    scene.export(OUT / "assembly_DRAFT.glb")
    (OUT / "verification.json").write_text(json.dumps(report, indent=2)+"\n")
    return report


def preview(parts):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.colors import to_rgba
    from mpl_toolkits.mplot3d.art3d import Poly3DCollection
    fig = plt.figure(figsize=(13, 7), facecolor="#f4f5f7")
    colors = {"bracket_enclosure": "#536278", "lid": "#8aa2be",
              "board_envelope_reference": "#29b88a", "ball_hardware_reference": "#d3a14d"}
    for index, elevation, azimuth, label in [(1, 22, 65, "Front / side"),
                                             (2, 65, -65, "Rear: enclosure open")]:
        ax = fig.add_subplot(1, 2, index, projection="3d", facecolor="#f4f5f7")
        triangles, facecolors = [], []
        for name, solid in parts.items():
            if index == 2 and name == "lid":
                continue
            if index == 1 and name == "board_envelope_reference":
                continue
            obj = mesh(solid)
            triangles.extend(obj.triangles)
            facecolors.extend([to_rgba(colors[name])]*len(obj.faces))
        # Sort all assembly triangles together to preserve inter-part occlusion.
        ax.add_collection3d(Poly3DCollection(triangles, facecolors=facecolors, shade=True))
        ax.set_xlim(-80, 80); ax.set_ylim(-70, 60); ax.set_zlim(-85, 60)
        ax.set_box_aspect((160, 130, 145)); ax.view_init(elev=elevation, azim=azimuth)
        ax.set_xlabel("Width (mm)"); ax.set_ylabel("Depth (mm)"); ax.set_zlabel("Height (mm)")
        ax.set_title(label)
    fig.suptitle("RideSync + printable GPS bracket | Yamaha Tracer 900 (2018)", fontsize=16)
    fig.text(.5, .025, "DRAFT — 94 mm bracket width; bike interface inferred from photos. Green: board envelope. Gold: ball hardware reference.",
             ha="center", fontsize=10)
    fig.savefig(OUT / "assembly_DRAFT.png", dpi=160)
    plt.close(fig)


if __name__ == "__main__":
    parts = build()
    report = export(parts)
    preview(parts)
    print(json.dumps(report, indent=2))
