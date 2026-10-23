#!/usr/bin/env python3
"""Traces the 8 ohm SPL curve of the Jensen C12K specification sheet into c12k_8ohm.csv.

Input: the sheet image as served by https://www.jensentone.com/vintage-ceramic/c12k
(specification-sheet-previews/c12k_spec_sheet.png, 1060 x 1574 pixels; not stored in the
repository). The axes are calibrated on the chart grid: 1 kHz at x = 607, 316.5 pixels per
decade; +110 dB at y = 906, 4.75 pixels per dB. The blue curve is followed column by column
from 1 kHz outwards: per column the blue pixel clusters are found and the one nearest to the
prediction from the recent slope is kept; where the red or green curve covers it the column is
skipped and the search window widens with the gap. The points are averaged over +-1/24 octave
and sampled every 1/12 octave from 20 Hz.

Usage: trace_chart.py <sheet.png> <out.csv> [--check check.png]
--check draws the traced points 6 pixels above the curve on a crop of the chart, to look at.
Needs numpy and Pillow (the repository venv).
"""
import argparse

import numpy as np
from PIL import Image, ImageDraw

X_1KHZ, X_PER_DECADE = 607.0, 316.5
Y_110DB, Y_PER_DB = 906.0, 4.75
TOP, BOTTOM, LEFT, RIGHT = 895, 1195, 75, 1015


def x_of(f):
    return X_1KHZ + X_PER_DECADE * np.log10(f / 1000.0)


def f_of(x):
    return 1000.0 * 10 ** ((x - X_1KHZ) / X_PER_DECADE)


def db_of(y):
    return 110.0 - (y - Y_110DB) / Y_PER_DB


def blue_clusters(a, x):
    col = a[TOP:BOTTOM, x]
    mask = (col[:, 2] - col[:, 0] > 60) & (col[:, 2] - col[:, 1] > 30)
    clusters = []
    for y in np.nonzero(mask)[0] + TOP:
        if clusters and y - clusters[-1][-1] <= 2:
            clusters[-1].append(y)
        else:
            clusters.append([y])
    return [float(np.mean(c)) for c in clusters]


def track(a, x0, y0, step):
    pts = {}
    last_x, last_y, slope = x0, y0, 0.0
    x = x0
    while LEFT <= x <= RIGHT:
        gap = abs(x - last_x)
        pred = last_y + slope * (x - last_x) * (1 if gap <= 1 else 0.5)
        clusters = blue_clusters(a, x)
        if clusters:
            best = min(clusters, key=lambda c: abs(c - pred))
            if abs(best - pred) < 6 + 1.5 * gap:
                if 0 < gap <= 3:
                    slope = 0.7 * slope + 0.3 * (best - last_y) / (x - last_x)
                pts[x] = best
                last_x, last_y = x, best
        x += step
    return pts


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("image")
    parser.add_argument("csv")
    parser.add_argument("--check")
    args = parser.parse_args()
    image = Image.open(args.image).convert("RGB")
    a = np.asarray(image).astype(int)
    start = int(round(x_of(1000.0)))
    y0 = min(blue_clusters(a, start))  # in the midband the SPL curve is the upper blue one
    pts = track(a, start, y0, 1)
    pts.update(track(a, start, y0, -1))
    xs = np.array(sorted(pts))
    f = f_of(xs)
    db = db_of(np.array([pts[x] for x in xs]))
    grid = 20.0 * 2.0 ** (np.arange(0, 12 * np.log2(9000 / 20) + 1) / 12)
    grid = grid[(grid >= f[0]) & (grid <= f[-1])]
    lf = np.log(f)
    values = []
    for g in grid:
        sel = np.abs(lf - np.log(g)) <= np.log(2) / 24
        values.append(db[sel].mean() if sel.any() else np.interp(np.log(g), lf, db))
    with open(args.csv, "w") as fh:
        fh.write("# Jensen C12K, 8 ohm: sound pressure on an IEC baffle (DIN 45575) at 1 W, 1 m, in dB SPL.\n")
        fh.write("# Traced from the frequency response chart of the Jensen C12K specification sheet\n")
        fh.write("# (https://www.jensentone.com/vintage-ceramic/c12k, sheet dated 02/07/2020) by\n")
        fh.write("# documentation/JaysTwin/trace_chart.py (the blue curve followed column by column,\n")
        fh.write("# axes calibrated on the chart grid), averaged over +-1/24 octave, sampled every 1/12 octave.\n")
        fh.write("# Reading accuracy about +-0.5 dB and +-1 % in frequency.\n")
        fh.write("# frequency_hz spl_db\n")
        for g, v in zip(grid, values):
            fh.write(f"{g:.1f} {v:.1f}\n")
    if args.check:
        draw = ImageDraw.Draw(image)
        for x in xs:
            draw.point((int(x), pts[x] - 6), fill=(255, 0, 255))
        image.crop((30, 890, 1040, 1220)).resize((2020, 660)).save(args.check)
    print(f"{len(xs)} columns traced, {f[0]:.0f} Hz to {f[-1]:.0f} Hz, {len(grid)} points written")


if __name__ == "__main__":
    main()
