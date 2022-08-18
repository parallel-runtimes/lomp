# ===-----------------------------------------------------------*- Python -*-===
#
#  Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===

import BMutils

otherLocks = ("O", "P", "X")  # OpenMP, pthread_mutex, exchange
otherLocks = ("O", "P")  # OpenMP, pthread_mutex
nonSpeculativeLocks = ("A", "B", "C", "M", "T", "U") + otherLocks
speculativeLocks = ("Q", "R")
runDesc = BMutils.runDescription(
    BMutils.getExecutable("microBM/locks"),
    {
        "C": nonSpeculativeLocks,
        # S only makes sense with icc or clang
        "S": speculativeLocks + nonSpeculativeLocks,
        "I": ("A", "T"),
        "M": speculativeLocks + ("C", "M"),
        "O": nonSpeculativeLocks,
        "X": nonSpeculativeLocks,
    },
    {
        "C": [str(i) for i in (1, 2, 4, 8, 16)],
        "X": [str(i) for i in (1, 2, 4, 8, 16)],
        "M": [str(i) for i in (0, 1, 2, 5, 10)],
    },
    "Locks",
)
BMutils.runBM(runDesc)
