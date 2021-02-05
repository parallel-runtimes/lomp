# ===-- schedulingEfficiency.py - Loop scheduling efficiency ---*- Python -*-===
#
#  Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
#  See https:llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===

#
# Efficiency for simple static scheduling
#


def efficiency(iterations, nthreads):
    """Compute the efficiency"""
    baseIterationsPerThread = iterations // nthreads
    remainder = iterations - baseIterationsPerThread * nthreads
    totalAvailable = nthreads * (baseIterationsPerThread + (1 if remainder != 0 else 0))
    return float(iterations) / totalAvailable


print("Theoretical Static Scheduling Efficiency")
nThreads = 10
print(nThreads, " threads")
print("n (number of chunks of work),      Efficiency")
for iterations in range(1, 101):
    print(iterations, ",", efficiency(iterations, nThreads) * 100, "%")
