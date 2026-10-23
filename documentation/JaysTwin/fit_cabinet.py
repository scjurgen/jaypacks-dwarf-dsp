#!/usr/bin/env python3
"""Fits a chain of cookbook biquads to a measured speaker response (c12k_8ohm.csv).

The model is the chain of dsp/Filters/SpeakerJensenC12K.h: two second-order highpasses (the
speaker's resonance and the steeper fall below it on the finite baffle), three peaking sections (the dip near 400 Hz, the presence peak, a cut
for the steep fall above 5 kHz) and two second-order lowpasses (the roll-off), plus a level
offset within 4 dB of the 500 Hz to 2 kHz mean. The
designs are those of dsp/Filters/Biquad.h (BiquadCoefficients), computed here in double.
Least squares in dB over 30 Hz to 8 kHz on the CSV's 1/12-octave grid, points above 6 kHz
(ragged cone break-up) weighted 0.3; Nelder-Mead on log frequency, log Q and gain, restarted
from its own result until it stops improving. Each section is bounded to its role (BOUNDS),
and above the data the chain must stay 25 dB below the passband: unbounded, the optimiser
trades a +40 dB peak against the level offset and parks a Q 12 lowpass resonance at 8.8 kHz,
which fits the curve but rings and lifts everything above it.

Usage: fit_cabinet.py [csv] [--rate 48000]
Prints the section table for the header and the fit error; numpy only.
"""
import argparse
import math
import pathlib

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
KINDS = ["HighPass", "HighPass", "Peak", "Peak", "Peak", "LowPass", "LowPass"]
# start: frequency, Q, gain (dB, peaks only)
START = [(110.0, 1.3, 0.0), (80.0, 0.6, 0.0), (400.0, 1.5, -5.0), (2400.0, 2.0, 5.0), (6500.0, 1.5, -15.0),
         (5000.0, 0.8, 0.0), (6000.0, 0.7, 0.0)]
# bounds per section, so that each keeps its role: frequency, Q, gain (min, max)
BOUNDS = [((60, 160), (0.5, 2.5), (0, 0)),       # resonance
          ((20, 160), (0.5, 1.0), (0, 0)),       # the steeper fall below it (finite baffle)
          ((250, 700), (0.5, 4.0), (-10, 0)),    # dip near 400 Hz
          ((1500, 3500), (0.7, 6.0), (0, 10)),   # presence peak
          ((5000, 9000), (0.5, 4.0), (-30, 0)),  # break-up cut
          ((3000, 8000), (0.5, 2.0), (0, 0)),    # roll-off
          ((3000, 9000), (0.5, 1.5), (0, 0))]    # roll-off
# above the data the chain must keep falling: at least this far below the passband level
STOPBAND_HZ = np.array([10000.0, 12000.0, 15000.0, 19000.0])
STOPBAND_DB = -25.0


def design(kind, fs, f, q, gain):
    """b0, b1, b2, a1, a2 as BiquadCoefficients::coefficients() computes them."""
    f = min(max(f, 1.0), 0.49 * fs)
    k = math.tan(math.pi * f / fs)
    kk = k * k
    if kind == "LowPass":
        norm = 1 / (1 + k / q + kk)
        b0 = kk * norm
        return b0, 2 * b0, b0, 2 * (kk - 1) * norm, (1 - k / q + kk) * norm
    if kind == "HighPass":
        norm = 1 / (1 + k / q + kk)
        return norm, -2 * norm, norm, 2 * (kk - 1) * norm, (1 - k / q + kk) * norm
    if kind == "Peak":
        v, v1 = 10 ** (abs(gain) / 20), 1.0
        if gain < 0:
            v, v1 = v1, v
        norm = 1 / (1 + v1 / q * k + kk)
        b1 = 2 * (kk - 1) * norm
        return (1 + v / q * k + kk) * norm, b1, (1 - v / q * k + kk) * norm, b1, (1 - v1 / q * k + kk) * norm
    raise ValueError(kind)


def response_db(params, freqs, fs):
    z = np.exp(-2j * np.pi * freqs / fs)
    h = np.ones_like(z)
    for kind, (f, q, g) in zip(KINDS, params):
        b0, b1, b2, a1, a2 = design(kind, fs, f, q, g)
        h *= (b0 + b1 * z + b2 * z * z) / (1 + a1 * z + a2 * z * z)
    return 20 * np.log10(np.abs(h))


def unpack(x):
    params = []
    for i, kind in enumerate(KINDS):
        f = math.exp(x[3 * i])
        q = math.exp(x[3 * i + 1])
        g = float(x[3 * i + 2]) if kind == "Peak" else 0.0
        params.append((f, q, g))
    return params, x[-1]


def nelder_mead(cost, x0, step, iterations):
    n = len(x0)
    simplex = [x0] + [x0 + step * np.eye(n)[i] for i in range(n)]
    values = [cost(x) for x in simplex]
    for _ in range(iterations):
        order = np.argsort(values)
        simplex = [simplex[i] for i in order]
        values = [values[i] for i in order]
        centroid = np.mean(simplex[:-1], axis=0)
        reflected = centroid + (centroid - simplex[-1])
        fr = cost(reflected)
        if fr < values[0]:
            expanded = centroid + 2 * (centroid - simplex[-1])
            fe = cost(expanded)
            simplex[-1], values[-1] = (expanded, fe) if fe < fr else (reflected, fr)
        elif fr < values[-2]:
            simplex[-1], values[-1] = reflected, fr
        else:
            contracted = centroid + 0.5 * (simplex[-1] - centroid)
            fc = cost(contracted)
            if fc < values[-1]:
                simplex[-1], values[-1] = contracted, fc
            else:
                simplex = [simplex[0] + 0.5 * (s - simplex[0]) for s in simplex]
                values = [cost(s) for s in simplex]
    best = int(np.argmin(values))
    return simplex[best], values[best]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", nargs="?", default=str(HERE / "c12k_8ohm.csv"))
    parser.add_argument("--rate", type=float, default=48000.0)
    args = parser.parse_args()
    data = np.loadtxt(args.csv)
    sel = (data[:, 0] >= 30) & (data[:, 0] <= 8000)
    freqs, target = data[sel, 0], data[sel, 1]
    weights = np.where(freqs > 6000, 0.3, 1.0)
    fs = args.rate
    passband = float(np.mean(target[(freqs > 500) & (freqs < 2000)]))

    def cost(x):
        params, offset = unpack(x)
        if abs(offset - passband) > 4.0:
            return 1e9
        for (f, q, g), (fb, qb, gb) in zip(params, BOUNDS):
            if not (fb[0] <= f <= fb[1] and qb[0] <= q <= qb[1] and gb[0] <= g <= gb[1]):
                return 1e9
        err = response_db(params, freqs, fs) + offset - target
        excess = np.maximum(response_db(params, STOPBAND_HZ, fs) - STOPBAND_DB, 0.0)
        return float(np.sum(weights * err * err) / np.sum(weights) + np.sum(excess * excess))

    x = []
    for f, q, g in START:
        x += [math.log(f), math.log(q), g]
    x = np.array(x + [passband])
    best = cost(x)
    for _ in range(40):
        x, value = nelder_mead(cost, x, 0.15, 3000)
        if best - value < 1e-6:
            break
        best = value
    params, offset = unpack(x)
    err = response_db(params, freqs, fs) + offset - target
    band = freqs <= 6000
    print(f"// fit at {fs:.0f} Hz to {pathlib.Path(args.csv).name}: rms {math.sqrt(best):.2f} dB (weighted), "
          f"30 Hz to 6 kHz max {np.max(np.abs(err[band])):.2f} dB, level offset {offset:.2f} dB SPL")
    for kind, (f, q, g) in zip(KINDS, params):
        print(f"{{BiquadFilterType::{kind}, {f:.1f}f, {q:.3f}f, {g:.2f}f}},")


if __name__ == "__main__":
    main()
