import math
import os

import FreeCAD as App
import Mesh
import Part


OUT_DIR = "/home/sz/coderepo/stm32/stm32_full/cad"
DOC_PATH = os.path.join(OUT_DIR, "robot_toy.FCStd")
STL_PATH = os.path.join(OUT_DIR, "robot_toy.stl")


def add_part(doc, name, shape, color):
    obj = doc.addObject("Part::Feature", name)
    obj.Shape = shape
    if getattr(obj, "ViewObject", None):
        obj.ViewObject.ShapeColor = color
    return obj


def rounded_box(width, depth, height, radius):
    box = Part.makeBox(width, depth, height)
    box.translate(App.Vector(-width / 2, -depth / 2, 0))
    try:
        return box.makeFillet(radius, box.Edges)
    except Exception:
        return box


def sphere(center, radius):
    shape = Part.makeSphere(radius)
    shape.translate(App.Vector(*center))
    return shape


def cylinder(center, radius, height, direction=(0, 0, 1)):
    shape = Part.makeCylinder(radius, height, App.Vector(*center), App.Vector(*direction))
    return shape


def wheel(center, radius=6, thickness=5):
    body = cylinder(
        (center[0], center[1] - thickness / 2, center[2]),
        radius,
        thickness,
        (0, 1, 0),
    )
    hub = cylinder(
        (center[0], center[1] - thickness / 2 - 0.15, center[2]),
        radius * 0.42,
        thickness + 0.3,
        (0, 1, 0),
    )
    return body.fuse(hub)


def make_robot():
    doc = App.newDocument("robot_toy")

    body = rounded_box(34, 22, 30, 3)
    body.translate(App.Vector(0, 0, 11))
    add_part(doc, "rounded_body", body, (0.15, 0.43, 0.85))

    belly = rounded_box(22, 2.2, 12, 1.4)
    belly.translate(App.Vector(0, -11.2, 21))
    add_part(doc, "front_panel", belly, (0.92, 0.94, 0.90))

    head = rounded_box(28, 20, 18, 3)
    head.translate(App.Vector(0, 0, 43))
    add_part(doc, "rounded_head", head, (0.18, 0.58, 0.78))

    left_eye = sphere((-7, -10.3, 53), 2.8)
    right_eye = sphere((7, -10.3, 53), 2.8)
    add_part(doc, "left_eye", left_eye, (0.03, 0.03, 0.03))
    add_part(doc, "right_eye", right_eye, (0.03, 0.03, 0.03))

    mouth = rounded_box(11, 1.2, 2, 0.6)
    mouth.translate(App.Vector(0, -10.7, 47.2))
    add_part(doc, "small_mouth", mouth, (0.03, 0.03, 0.03))

    antenna_stem = cylinder((0, 0, 61), 1.2, 9)
    antenna_tip = sphere((0, 0, 71), 3)
    add_part(doc, "antenna_stem", antenna_stem, (0.9, 0.2, 0.2))
    add_part(doc, "antenna_tip", antenna_tip, (0.9, 0.2, 0.2))

    left_arm = cylinder((-20, 0, 30), 2.8, 16, (0, 0, -1))
    left_arm.rotate(App.Vector(-20, 0, 30), App.Vector(0, 1, 0), -25)
    right_arm = cylinder((20, 0, 30), 2.8, 16, (0, 0, -1))
    right_arm.rotate(App.Vector(20, 0, 30), App.Vector(0, 1, 0), 25)
    add_part(doc, "left_arm", left_arm, (0.15, 0.43, 0.85))
    add_part(doc, "right_arm", right_arm, (0.15, 0.43, 0.85))

    add_part(doc, "left_hand", sphere((-25.7, 0, 16.5), 4), (0.95, 0.76, 0.25))
    add_part(doc, "right_hand", sphere((25.7, 0, 16.5), 4), (0.95, 0.76, 0.25))

    add_part(doc, "left_wheel", wheel((-13, -12.5, 8)), (0.08, 0.08, 0.09))
    add_part(doc, "right_wheel", wheel((13, -12.5, 8)), (0.08, 0.08, 0.09))

    left_foot = rounded_box(11, 14, 5, 1.8)
    left_foot.translate(App.Vector(-9, 0, 1))
    right_foot = rounded_box(11, 14, 5, 1.8)
    right_foot.translate(App.Vector(9, 0, 1))
    add_part(doc, "left_foot", left_foot, (0.95, 0.76, 0.25))
    add_part(doc, "right_foot", right_foot, (0.95, 0.76, 0.25))

    ring_outer = cylinder((0, 11.5, 45), 5.2, 2.2, (0, 1, 0))
    ring_inner = cylinder((0, 11.35, 45), 2.7, 2.6, (0, 1, 0))
    ring = ring_outer.cut(ring_inner)
    add_part(doc, "back_hanging_loop", ring, (0.18, 0.58, 0.78))

    base = rounded_box(38, 26, 2, 1.5)
    add_part(doc, "flat_display_base", base, (0.18, 0.18, 0.18))

    doc.recompute()
    doc.saveAs(DOC_PATH)
    Mesh.export(doc.Objects, STL_PATH)
    return DOC_PATH, STL_PATH


if __name__ == "__main__":
    fcstd, stl = make_robot()
    print(fcstd)
    print(stl)
