# ===-----------------------------------------------------------*- Python -*-===
#
#  Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===

import BMutils

runDesc = BMutils.runDescription(
    BMutils.getExecutable("futex"), {"R": ("",), "L": ("",),}, {}, "Futex"
)
BMutils.runBM(runDesc)
