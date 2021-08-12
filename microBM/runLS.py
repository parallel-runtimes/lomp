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
        #       "ru0", "rm0", "wu0", "wm0", "au0", "am0",),
        "P": ("ru", "ru -1", "rm", "wu", "wu -1", "wm", "au", "au -1","am"),
        "R": ("a", "w"),
        "S": ("ru", "rm", "wu", "wm", "au", "am"),
        "V": ("",),
    },
    # P,R,S can all take an extra argument to choose the active core, or -1 for all
    # We express that here, but commented out, because you probably don't want to run
    # all six P,S cases with all threads, but just pick one of them
    #
    # For now this is faked up above by adding in the extra cases there...
    {
     # "P": ("0","-1"),
     # "R": ("0", "-1"),
     # "S": ("0","-1"),
     "V": ("0","-1"),        
    },
    # Output file name prefix
    "LS",
)
# None => all :-)
tests = None
# Choose a subset of tests to run here.
tests = ("V",)

BMutils.runBM(runDesc, tests)
