"""Example VectorKernel script.

This runs inside the app when embedded Python is enabled (Alt+P).

It uses the built-in `vk` module exposed by the application.
"""

import vk

vk.write("Hello from Python!\n")

# Add a bright line.
vk.add_line(
    50, 50, 0,
    400, 220, 0,
    1.0, 0.2, 0.2, 1.0,
    3.0,
    150,
)

# Add a label.
vk.add_text(
    "Python-added entities persist (EntityTag::User)",
    60, 260, 0,
    1.0,
    1.0, 1.0, 1.0, 1.0,
    200,
)
