# ===-----------------------------------------------------------*- Python -*-===
#
#  Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===

import BMutils

centralizedBarriers = (
    "AtomicLBW1",
    "AtomicLBW2",
    "AtomicLBW4",
    "AtomicLBW64",
    "AtomicLBW8",
    "AtomicNaive",
    "DT16LBW4",
    "DT16Naive",
    "DT2LBW4",
    "DT2Naive",
    "DT4LBW4",
    "DT4Naive",
    "DT8LBW4",
    "DT8Naive",
    "FT16AtomicLBW4",
    "FT16AtomicNaive",
    "FT16FlagLBW4",
    "FT16FlagNaive",
    "FT2AtomicLBW4",
    "FT2AtomicNaive",
    "FT2FlagLBW4",
    "FT2FlagNaive",
    "FT4AtomicLBW4",
    "FT4AtomicNaive",
    "FT4FlagLBW4",
    "FT4FlagNaive",
    "FT8AtomicLBW4",
    "FT8AtomicNaive",
    "FT8FlagLBW4",
    "FT8FlagNaive",
    "FlagLBW1",
    "FlagLBW2",
    "FlagLBW4",
    "FlagLBW64",
    "FlagLBW8",
    "FlagNaive",
)

distributedBarriers = (
    "AllToAllAtomic",
    "AtomicUpDown",
    "Dissemination",
    "Hypercube",
    "omp",
)

runDesc = BMutils.runDescription(
    BMutils.getExecutable("barriers"),
    {"LILO": ("",), "LIRO": ("",), "RILO": ("",)},
    {
        "LILO": centralizedBarriers + distributedBarriers,
        "RILO": centralizedBarriers,
        "LIRO": centralizedBarriers,
    },
    "Barriers_",
)
BMutils.runBM(runDesc)
