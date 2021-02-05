# ===-- toCSV.py - CSV converter tool --------------------------*- Python -*-===
#
#  Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
#  See https:llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===

#
# Convert a file to CSV format, expanding units

import sys


def convertSI(s):
    """Convert a measurement with a range suffix into a suitably scaled value"""
    du = s.split()
    if len(du) != 2:
        return s
    units = du[1] if len(du) == 2 else " "
    # http://physics.nist.gov/cuu/Units/prefixes.html
    factor = {
        "Y": "e24",
        "Z": "e21",
        "E": "e18",
        "P": "e15",
        "T": "e12",
        "G": "e9",
        "M": "e6",
        "k": "e3",
        " ": "",
        "m": "e-3",
        "u": "e-6",
        "n": "e-9",
        "p": "e-12",
        "f": "e-15",
        "a": "e-18",
        "z": "e-21",
        "y": "e-24",
    }[units[0]]
    return du[0] + factor


for line in sys.stdin:
    if "," in line:
        items = line.split(",")
        # Don't do anything with the first column
        print(", ".join([items[0]] + [convertSI(s) for s in items[1:]]))
    else:
        print(line, end="")
