import os

import FreeCAD as App
import Mesh
import Part


OUT_DIR = "/home/sz/coderepo/stm32/stm32_full/cad"
DOC_PATH = os.path.join(OUT_DIR, "rocket_whistle_toy.FCStd")
STL_PATH = os.path.join(OUT_DIR, "rocket_whistle_toy.stl")
STEP_PATH = os.path.join(OUT_DIR, "rocket_whistle_toy.step")


def fillet(shape, radius):
    try:
        return shape.makeFillet(radius, shape.Edges)
    except Exception:
        return shape


def add(doc, name, shape, color=None):
    obj = doc.addObject("Part::Feature", name)
    obj.Shape = shape
    if color and getattr(obj, "ViewObject", None):
        obj.ViewObject.ShapeColor = color
    return obj


def box(width, depth, height, center=(0, 0, 0), radius=0):
    shape = Part.makeBox(width, depth, height)
    shape.translate(App.Vector(center[0] - width / 2, center[1] - depth / 2, center[2]))
    return fillet(shape, radius) if radius else shape


def cyl_x(center, radius, length):
    return Part.makeCylinder(radius, length, App.Vector(center[0] - length / 2, center[1], center[2]), App.Vector(1, 0, 0))


def cyl_z(center, radius, height):
    return Part.makeCylinder(radius, height, App.Vector(center[0], center[1], center[2]), App.Vector(0, 0, 1))


def make_fin(x, y_sign):
    pts = [
        App.Vector(x - 12, y_sign * 8, 0),
        App.Vector(x + 12, y_sign * 8, 0),
        App.Vector(x - 1, y_sign * 19, 0),
        App.Vector(x - 12, y_sign * 8, 0),
    ]
    face = Part.Face(Part.makePolygon(pts))
    fin = face.extrude(App.Vector(0, 0, 4.5))
    return fillet(fin, 0.8)


def make_star(center, r1=3.2, r2=1.4, height=0.75):
    pts = []
    for i in range(10):
        angle = i * 36
        radius = r1 if i % 2 == 0 else r2
        pts.append(App.Vector(
            center[0] + radius * App.Base.Vector(1, 0, 0).x,
            center[1],
            center[2],
        ))
    # Use a simple round badge instead of a true star to keep the model robust.
    return cyl_z((center[0], center[1], center[2]), r1, height)


def make_toy():
    doc = App.newDocument("rocket_whistle_toy")

    main = box(68, 21, 17, (0, 0, 0), 3.5)
    nose = Part.makeCone(10.5, 1.2, 18, App.Vector(34, 0, 8.5), App.Vector(1, 0, 0))
    nose = fillet(nose, 0.8)
    shell = main.fuse(nose)

    left_fin = make_fin(-19, 1)
    right_fin = make_fin(-19, -1)
    bottom_fin = box(20, 5, 10, (-18, 0, -1.8), 1.2)
    bottom_fin.rotate(App.Vector(-18, 0, 0), App.Vector(1, 0, 0), 90)
    shell = shell.fuse(left_fin).fuse(right_fin)

    # Pealess whistle internals. Dimensions leave printable walls around the
    # windway and chamber; print flat on the bottom face, mouthpiece to the left.
    windway = box(37, 6.2, 3.0, (-19.5, 0, 6.7), 0.25)
    inlet_rounding = cyl_x((-37, 0, 8.2), 4.0, 2.5)
    chamber = box(30, 14, 10.5, (10, 0, 4.5), 1.4)
    top_window = box(15, 13, 9.5, (-1.8, 0, 10.2), 0.7)
    tuning_slot = box(7, 10, 5, (25.8, 0, 9.8), 0.6)
    mouth_open = box(4, 8.5, 5.2, (-35.5, 0, 5.8), 0.6)
    strap_hole = cyl_z((42.5, 0, 5.5), 3.0, 8)

    toy = shell.cut(windway).cut(chamber).cut(top_window).cut(tuning_slot).cut(mouth_open).cut(strap_hole)

    # A small raised lip near the sound window makes the edge visible and gives
    # the airflow a sharper target after slicing.
    lip = box(1.1, 12, 2.3, (5.5, 0, 9.1), 0.15)
    lip.rotate(App.Vector(5.5, 0, 9.1), App.Vector(0, 1, 0), -12)
    toy = toy.fuse(lip)

    add(doc, "one_piece_rocket_whistle_body", toy, (0.92, 0.18, 0.16))

    add(doc, "left_side_fin_detail", left_fin, (0.12, 0.38, 0.88))
    add(doc, "right_side_fin_detail", right_fin, (0.12, 0.38, 0.88))
    add(doc, "cockpit_window", cyl_x((12, -10.8, 11.3), 3.2, 0.9), (0.1, 0.75, 0.95))
    add(doc, "front_badge", cyl_z((-10, -10.7, 12.3), 2.4, 0.85), (0.98, 0.76, 0.2))
    add(doc, "rear_band", box(2.1, 22.4, 17.4, (-27.5, 0, 0), 0.4), (0.1, 0.1, 0.12))

    doc.recompute()
    doc.saveAs(DOC_PATH)
    Mesh.export(doc.Objects, STL_PATH)
    Part.export(doc.Objects, STEP_PATH)
    print(DOC_PATH)
    print(STL_PATH)
    print(STEP_PATH)


if __name__ == "__main__":
    make_toy()
