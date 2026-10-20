#!/usr/bin/env python3
"""Prints the integer tables sleeve.h embeds, so they can be checked or regenerated.

- the cosine of every whole degree from 0 to 90, in units of 1/65536;
- the sRGB transfer function's inverse: entry v is round(65535 * linear(v / 255));
- Björn Ottosson's OKLab-to-linear-sRGB matrices, in units of 1/65536.
"""

import math


def rows(values, per_line=12, indent="  "):
    out = []
    for i in range(0, len(values), per_line):
        out.append(indent + ", ".join(str(v) for v in values[i:i + per_line]) + ",")
    return "\n".join(out)


def linear(v):
    c = v / 255
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


cosines = [round(65536 * math.cos(math.radians(d))) for d in range(91)]
transfer = [round(65535 * linear(v)) for v in range(256)]
lab_to_lms = [
    [1.0, 0.3963377774, 0.2158037573],
    [1.0, -0.1055613458, -0.0638541728],
    [1.0, -0.0894841775, -1.2914855480],
]
lms_to_rgb = [
    [4.0767416621, -3.3077115913, 0.2309699292],
    [-1.2684380046, 2.6097574011, -0.3413193965],
    [-0.0041960863, -0.7034186147, 1.7076147010],
]

print("cosine by degree, 0 to 90:")
print(rows(cosines))
print("sRGB to linear:")
print(rows(transfer))
print("OKLab to LMS':", [[round(65536 * x) for x in r] for r in lab_to_lms])
print("LMS to linear sRGB:", [[round(65536 * x) for x in r] for r in lms_to_rgb])
