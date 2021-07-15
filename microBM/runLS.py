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
        # "P": ("ru", "rm", "wu", "wm", "au", "am",
        #       "rua", "rma", "wua", "wma", "aua", "ama",),
        "P": ("ru", "rua", ),
        "R": ("a", "w"),
        "S": ("ru", "rm", "wu", "wm", "au", "am"),
        "V": ("",),
    },
    # P,R,S can all take an extra argument to choose the active core, or -1 for all
    # We express that here, but commented out, because you probably don't want to run
    # all six P,S cases with all threads, but just pick one of them
    {
#    "P": ("0","-1"),
     "P": ("-1",),
#    "R": ("0", "-1"),
     "R": ("-1", ),
     "S": ("0","-1"),},
    "LS",
)
# None => all :-)
tests = None
# Choose a subset of tests to run here.
tests = ("P",)

BMutils.runBM(runDesc, tests)
