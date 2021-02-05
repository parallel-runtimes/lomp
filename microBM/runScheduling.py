# ===-----------------------------------------------------------*- Python -*-===
#
#  Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===

import BMutils
import sys

scriptDir = sys.path[0]

# This is more sneaky, since we invoke another python script,
# because we want to do measurements at different numbers
# of threads, but our runtime doesn't allow team size changes
# so we have to run each test as a separate execution.
libraries = ["LLVM", "LOMP"]
# libraries = ["LLVM"]
# libraries = ["LOMP"]
schedules = ("static", "static1", "monotonic", "nonmonotonic", "guided")
runDesc = BMutils.runDescription(
    "python3 " + scriptDir + "/runSchedTest.py",
    {"increasing_": schedules, "square_": schedules, "random_": schedules},
    {"increasing_": libraries, "square_": libraries, "random_": libraries,},
    "Sched_",
)
BMutils.runBM(runDesc)
