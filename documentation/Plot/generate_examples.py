#!/usr/bin/env python3

"""Generates the example data files rendered in README.md's Examples section."""

import math


def write_overlay_example(path: str) -> None:
    with open(path, "w") as f:
        f.write('@New plot: title="sin, cos, and a damped sine"\n')
        f.write("#sin\n")
        for i in range(200):
            x = i * 0.05
            f.write(f"{x} {math.sin(x)}\n")
        f.write("#cos\n")
        for i in range(200):
            x = i * 0.05
            f.write(f"{x} {math.cos(x)}\n")
        f.write("#damped sin(3x)\n")
        for i in range(200):
            x = i * 0.05
            f.write(f"{x} {math.exp(-0.2 * x) * math.sin(3 * x)}\n")


def write_subplots_example(path: str) -> None:
    with open(path, "w") as f:
        f.write('@New plot: title="linear"\n#y=x\n')
        for i in range(50):
            x = i * 0.2
            f.write(f"{x} {x}\n")
        f.write('@New plot: title="quadratic"\n#y=x^2\n')
        for i in range(50):
            x = i * 0.2
            f.write(f"{x} {x * x}\n")
        f.write('@New plot: title="exponential (log y)" logy=true\n#y=exp(x)\n')
        for i in range(50):
            x = i * 0.2
            f.write(f"{x} {math.exp(x)}\n")
        f.write('@New plot: title="damped sine"\n#y=exp(-0.3x)*sin(2x)\n')
        for i in range(100):
            x = i * 0.1
            f.write(f"{x} {math.exp(-0.3 * x) * math.sin(2 * x)}\n")


if __name__ == "__main__":
    write_overlay_example("example_overlay.txt")
    write_subplots_example("example_subplots.txt")
    print("wrote example_overlay.txt and example_subplots.txt")
