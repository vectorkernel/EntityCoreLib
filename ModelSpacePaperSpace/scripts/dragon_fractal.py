"""Heighway dragon (simple) using vk.add_line.

Alt+P to run. Tune iterations for performance.
"""

import vk

# Basic L-system for the dragon curve.

def dragon_instructions(n):
    s = "F"
    for _ in range(n):
        s = s.replace("F", "F+G").replace("G", "F-G")
    return s

# Interpret with turtle-like state.

def draw(n=10, step=4.0, angle_deg=90.0):
    import math
    angle = math.radians(angle_deg)
    x, y = 200.0, 200.0
    heading = 0.0

    instr = dragon_instructions(n)
    vk.write(f"[dragon] len={len(instr)}\n")

    for ch in instr:
        if ch in ("F", "G"):
            nx = x + step * math.cos(heading)
            ny = y + step * math.sin(heading)
            vk.add_line(x, y, 0, nx, ny, 0, 0.2, 0.8, 1.0, 1.0, 2.0, 120)
            x, y = nx, ny
        elif ch == "+":
            heading += angle
        elif ch == "-":
            heading -= angle

vk.write("Drawing dragon...\n")
draw(n=12)
