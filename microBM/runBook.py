# ===-----------------------------------------------------------*- Python -*-===
#
#  Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===

import BMutils

def main():
    libraries = ["LLVM"]
    schedules = ("static", "static1", "monotonic", "nonmonotonic", "guided")
    runs = {
        "Chapter3": [
            BMutils.runDescription(
                BMutils.getExecutable("loadsStores"),
                # For the moment don't run 'N' it doesn't seem to work...
                {
                    "L": ("",),
                    "M": ("",),
                    "P": ("wm", "wu",),
                    "S": ("wm", "wu"),
                    "V": ("",),
                },
                {},
                "LS",
            )
        ],
        "Chapter6": [
            BMutils.runDescription(
                BMutils.getExecutable("locks"),
                {
                    "C": ("A", "C", "T"),
                    "I": ("A", "T"),
                    "O": ("M", "U", "T", "C", "B"),
                    "X": ("M", "U", "T", "C", "B"),
                    "M": ("M", "Q", "R", "C"),
                },
                {"C": ("1", "8"), "M": ("0", "1", "2", "5"), "X": ("1", "8"),},
                "Locks",
            ),
            BMutils.runDescription(
                BMutils.getExecutable("atomics"),
                {"I": ("f", "e", "t",),},
                {},
                "Atomics",
            ),
            BMutils.runDescription(
                BMutils.getExecutable("futex"), {"L": ("",), "R": ("",)}, {}, "Futex",
            ),
        ],
        "Chapter7": [
            BMutils.runDescription(
                BMutils.getExecutable("loadsStores"),
                # Also need V but we ran that for ch3 already
                {"R": ("a", "w"),},
                {},
                "LS",
            ),
            BMutils.runDescription(
                BMutils.getExecutable("barriers"),
                {"LILO": ("",), "LIRO": ("",), "RILO": ("",)},
                {
                    "LILO": {
                        "AllToAllAtomic",
                        "AtomicUpDown",
                        "omp",
                        "DT2LBW4",
                        "DT4LBW4",
                        "DT8LBW4",
                        "DT16LBW4",
                        "FT2FlagLBW4",
                        "FT4FlagLBW4",
                        "FT8FlagLBW4",
                        "FT16FlagLBW4",
                    },
                    "RILO": {
                        "AtomicNaive",
                        "DT4Naive",
                        "FT4AtomicNaive",
                        "FlagNaive",
                        "AtomicLBW1",
                        "AtomicLBW2",
                        "AtomicLBW4",
                        "AtomicLBW8",
                        "AtomicLBW64",
                    },
                    "LIRO": {
                        "DT2Naive",
                        "DT4Naive",
                        "DT8Naive",
                        "DT16Naive",
                        "FT2AtomicNaive",
                        "FT4AtomicNaive",
                        "FT8AtomicNaive",
                        "FT16AtomicNaive",
                    },
                },
                "Barriers_",
            ),
        ],
        "Chapter8": [
            BMutils.runDescription(
                "python3 runSchedTest.py",
                {"increasing_": schedules, "square_": schedules, "random_": schedules},
                {"increasing_": libraries, "square_": libraries, "random_": libraries,},
                "Sched_",
            )
        ],
    }

    validChapters = sorted(runs.keys())
    from optparse import OptionParser

    options = OptionParser()
    (options, args) = options.parse_args()
    args = ("3", "6", "7", "8") if not args else args
    for chapter in ["Chapter" + a for a in args]:
        if chapter not in validChapters:
            print("Valid chapters are " + ", ".join(validChapters))
            continue
        runDescs = runs[chapter]
        print("Runs for ", chapter)
        for r in runDescs:
            BMutils.runBM(r)


main()
