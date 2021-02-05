# ===-----------------------------------------------------------*- Python -*-===
#
#  Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===

import BMutils

runDesc = BMutils.runDescription(
    BMutils.getExecutable("loadsStores"),
    # For the moment don't run 'N' it doesn't seem to work...
    {
        "L": ("",),
        "M": ("",),
        # "N": ("",),
        "P": ("ru", "rm", "wu", "wm", "au", "am"),
        "R": ("a", "w"),
        "S": ("ru", "rm", "wu", "wm", "au", "am"),
        "V": ("",),
    },
    {},
    "LS",
)
# Choose a subset of tests to run here.
tests = ("P",)
# None => all :-)
tests = None

BMutils.runBM(runDesc, tests)
