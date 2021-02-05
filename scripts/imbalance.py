# ===-- atomicsTheory.py - Theoret. perf of triangular loops ---*- Python -*-===
#
#  Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
#  See https:llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===
#
# This file contains code which demonstrates the expected theoretical
# performance of static schedules on triangular loops.

# Just evaluate the sum of the iteration times directly;
# We could do it analytically, but for small cases this if fine.
def cyclic1Time(me, nthreads, count):
    myIterations = range(me + 1, count + 1, nthreads)
    print("Cyclic1 ", me, ",", sum(myIterations))
    return sum(myIterations)


def blockedTime(me, nthreads, count):
    wholeIters = count // nthreads
    leftover = count % nthreads
    if me < leftover:
        myBase = 1 + me * (wholeIters + 1)
        myEnd = myBase + wholeIters
    else:
        myBase = 1 + leftover + me * wholeIters
        myEnd = myBase + wholeIters - 1

    print("Blocked ", me, ": ", sum(range(myBase, myEnd + 1)))
    return sum(range(myBase, myEnd + 1))


def efficiency(distribution):
    nthreads = len(distribution)
    totalWork = sum(distribution)
    totalAvailableWork = nthreads * max(distribution)
    return float(totalWork) / totalAvailableWork


threads = 32
iterations = 1000
workCyclic1 = [cyclic1Time(t, threads, iterations) for t in range(threads)]
workBlocked = [blockedTime(t, threads, iterations) for t in range(threads)]

print("Cyclic1: work:", sum(workCyclic1), " efficiency:", efficiency(workCyclic1))
print("Blocked: work:", sum(workBlocked), " efficiency:", efficiency(workBlocked))
