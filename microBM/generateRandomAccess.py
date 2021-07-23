# ===-- generateRandomAccess.py - Random access stream ---------*- Python -*-===
#
#  Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===

# Use python to generate a random sequence of accesses to elements in an array,
# where each element is accessed once.
import random


def access(action, pos):
    print("  Array[", pos, "]." + action + "();")


# Generate the random access functions and table of function pointers.
def generateFunctionName(opName, needArg):
    return (
        "static void do"
        + opName
        + "s(alignedUint32 *"
        + ("Array" if needArg else "")
        + ")"
    )


print(generateFunctionName("Load", True) + ";")
print(generateFunctionName("Store", True) + ";")
print(generateFunctionName("AtomicInc", True) + ";")

numStores = 32
for n in range(numStores):
    print(generateFunctionName(str(n) + "Store", n != 0) + ";")

print("static Operation writeFns[] = {")
for n in range(numStores):
    print("do" + str(n) + "Stores,")
print("};")

numElements = 256
print ("#define measurementArraySize ", numElements)

indices = list(range(numElements))
random.shuffle(indices)

print(generateFunctionName("Load", True))
print("{")
for i in indices:
    access("load", i)
print("}")

random.shuffle(indices)
print(generateFunctionName("Store", True))
print("{")
for i in indices:
    access("store", i)
print("}")

random.shuffle(indices)
print(generateFunctionName("AtomicInc", True))
print("{")
for i in indices:
    access("atomicInc", i)
print("}")

for n in range(numStores):
    random.shuffle(indices)
    print(generateFunctionName(str(n) + "Store", n != 0))
    print("{")
    if n > 0:
        for i in indices[: n - 1]:
            access("store", i)
        access("storeRelease", indices[n - 1])
    print("}")
