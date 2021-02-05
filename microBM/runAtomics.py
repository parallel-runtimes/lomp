# ===-----------------------------------------------------------*- Python -*-===
#
#  Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===

#
# Atomic benchmarks
#
import BMutils

runDesc = BMutils.runDescription(
    BMutils.getExecutable("atomics"),
    {"I": ("e", "f", "i", "t"), "B": ("",)},
    {},
    "Atomics",
)

BMutils.runBM(runDesc, ("I",))  # Subset of benchmarks
