# ===-----------------------------------------------------------*- Python -*-===
#
#  Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===

#
# Generate functions with different numbers of arguments so that we can see how they are passed.
#
def generateDefinition(n):
    print("void f%d(" % n, end="")
    for i in range(n):
        print("void * arg%d" % (i + 1), end="")
        if i != n - 1:
            print(", ", end="")
            if (i & 3) == 3:
                print("\n%s" % (" " * (15 if n < 10 else 16)), end="")
    print(")", end="")


def generateExtern(n):
    print("extern ", end="")
    generateDefinition(n)
    print(";")


def generateCall(n):
    print("    f%d(" % n, end="")
    for i in range(n):
        print("(void *)%d" % i, end="")
        if i != n - 1:
            print(", ", end="")
            if (i & 3) == 3:
                print("\n%s" % (" " * (7 if n < 10 else 8)), end="")
    print(");")


def generateCalls(n):
    print("void test()\n{")
    for i in range(0, n + 1):
        generateCall(i)
    print("}")


for i in range(0, 17):
    generateExtern(i)
for i in range(0, 17):
    generateCalls(i)
