import os

import FreeCAD as App
import Mesh
import Part


OUT_DIR = "/home/sz/coderepo/stm32/stm32_full/cad"
DOC_PATH = os.path.join(OUT_DIR, "robot_toy_deluxe.FCStd")
STL_PATH = os.path.join(OUT_DIR, "robot_toy_deluxe.stl")
STEP_PATH = os.path.join(OUT_DIR, "robot_toy_deluxe.step")


def add(doc, name, shape, color=None):
    obj = doc.addObject("Part::Feature", name)
    obj.Shape = shape
    if color and getattr(obj, "ViewObject", None):
        obj.ViewObject.ShapeColor = color
    return obj


def fillet(shape, radius):
    try:
        return shape.makeFillet(radius, shape.Edges)
    except Exception:
        return shape


def box(width, depth, height, center=(0, 0, 0), radius=0):
    shape = Part.makeBox(width, depth, height)
    shape.translate(App.Vector(-width / 2 + center[0], -depth / 2 + center[1], center[2]))
    return fillet(shape, radius) if radius else shape


def cyl(center, radius, height, direction=(0, 0, 1)):
    return Part.makeCylinder(radius, height, App.Vector(*center), App.Vector(*direction))


def sphere(center, radius):
    shape = Part.makeSphere(radius)
    shape.translate(App.Vector(*center))
    return shape


def capsule_x(center, length, radius):
    bar = cyl((center[0] - length / 2, center[1], center[2]), radius, length, (1, 0, 0))
    return bar.fuse(sphere((center[0] - length / 2, center[1], center[2]), radius)).fuse(
        sphere((center[0] + length / 2, center[1], center[2]), radius)
    )


def screw(center, radius=1.25, slot_len=3.6):
    head = cyl((center[0], center[1], center[2]), radius, 0.8, (0, -1, 0))
    slot = box(slot_len, 0.9, 0.35, (center[0], center[1] - 0.5, center[2] - 0.18), 0.15)
    return head.fuse(slot)


def track_side(x):
    tread = box(13, 29, 8, (x, 0, 2.2), 2.2)
    rollers = []
    for z in [3.2, 6.0]:
        for y in [-9.5, 0, 9.5]:
            rollers.append(cyl((x, y - 6.8, z), 2.25, 2.2, (0, 1, 0)))
    shape = tread
    for roller in rollers:
        shape = shape.fuse(roller)
    return shape


def make_deluxe_robot():
    doc = App.newDocument("robot_toy_deluxe")

    base = box(49, 36, 4, (0, 0, 0), 1.8)
    add(doc, "thin_rounded_display_base", base, (0.14, 0.14, 0.16))

    add(doc, "left_rubber_track", track_side(-16), (0.05, 0.05, 0.06))
    add(doc, "right_rubber_track", track_side(16), (0.05, 0.05, 0.06))
    add(doc, "center_chassis", box(24, 26, 9, (0, 0, 3.5), 2.5), (0.18, 0.22, 0.26))

    body = box(37, 25, 31, (0, 0, 12), 4)
    add(doc, "soft_rounded_body_shell", body, (0.1, 0.38, 0.82))

    waist = capsule_x((0, -13.1, 17), 22, 3.1)
    add(doc, "front_gold_bumper", waist, (0.95, 0.72, 0.22))

    panel = box(27, 2.2, 18, (0, -13.4, 19.5), 1.8)
    add(doc, "inset_front_control_panel", panel, (0.9, 0.93, 0.9))

    for x, z in [(-8, 28), (0, 28.4), (8, 28), (-5, 22), (5, 22)]:
        add(doc, f"front_button_{x}_{z}", cyl((x, -14.8, z), 1.8, 1.1, (0, -1, 0)), (0.9, 0.18, 0.2))

    head = box(33, 24, 21, (0, 0, 45), 4)
    add(doc, "large_rounded_head_shell", head, (0.12, 0.58, 0.78))

    screen = box(24, 2.1, 12, (0, -12.9, 49.2), 2)
    add(doc, "glossy_face_screen", screen, (0.02, 0.025, 0.03))

    add(doc, "left_eye_lens", cyl((-6.5, -14.2, 53.2), 2.45, 1.1, (0, -1, 0)), (0.35, 0.95, 1.0))
    add(doc, "right_eye_lens", cyl((6.5, -14.2, 53.2), 2.45, 1.1, (0, -1, 0)), (0.35, 0.95, 1.0))
    smile = capsule_x((0, -14.3, 47.2), 8.5, 0.75)
    smile.rotate(App.Vector(0, -14.3, 47.2), App.Vector(0, 1, 0), 0)
    add(doc, "small_soft_smile", smile, (0.35, 0.95, 1.0))

    add(doc, "left_side_ear", cyl((-19.3, 0, 51), 5, 2.5, (1, 0, 0)), (0.95, 0.72, 0.22))
    add(doc, "right_side_ear", cyl((16.8, 0, 51), 5, 2.5, (1, 0, 0)), (0.95, 0.72, 0.22))
    add(doc, "left_ear_cap", cyl((-20.7, 0, 51), 2.1, 0.8, (1, 0, 0)), (0.05, 0.05, 0.06))
    add(doc, "right_ear_cap", cyl((20.0, 0, 51), 2.1, 0.8, (1, 0, 0)), (0.05, 0.05, 0.06))

    add(doc, "antenna_stem", cyl((0, 0, 65), 1.15, 9), (0.9, 0.16, 0.18))
    add(doc, "antenna_ball", sphere((0, 0, 75.4), 3.2), (0.9, 0.16, 0.18))

    for side, x, angle in [("left", -22, -28), ("right", 22, 28)]:
        arm = cyl((x, -1, 34), 2.7, 17, (0, 0, -1))
        arm.rotate(App.Vector(x, -1, 34), App.Vector(0, 1, 0), angle)
        add(doc, f"{side}_angled_arm", arm, (0.1, 0.38, 0.82))
        hand_x = -28.2 if side == "left" else 28.2
        add(doc, f"{side}_round_gripper", sphere((hand_x, -1, 20.5), 4.4), (0.95, 0.72, 0.22))
        add(doc, f"{side}_gripper_cut_line", box(5.6, 1, 1.1, (hand_x, -4.9, 20.5), 0.2), (0.05, 0.05, 0.06))

    for x in [-14, 14]:
        for z in [27.8, 14.8]:
            add(doc, f"body_screw_{x}_{z}", screw((x, -14.2, z)), (0.12, 0.12, 0.14))

    loop_outer = cyl((0, 13.4, 50), 6.1, 2.6, (0, 1, 0))
    loop_inner = cyl((0, 13.2, 50), 3.4, 3.0, (0, 1, 0))
    add(doc, "rounded_back_hanging_loop", loop_outer.cut(loop_inner), (0.12, 0.58, 0.78))

    badge = cyl((0, -14.7, 36), 3.2, 0.9, (0, -1, 0))
    add(doc, "tiny_chest_badge", badge, (0.95, 0.72, 0.22))

    doc.recompute()
    doc.saveAs(DOC_PATH)
    Mesh.export(doc.Objects, STL_PATH)
    Part.export(doc.Objects, STEP_PATH)
    print(DOC_PATH)
    print(STL_PATH)
    print(STEP_PATH)


if __name__ == "__main__":
    make_deluxe_robot()
