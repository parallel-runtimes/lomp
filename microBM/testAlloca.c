//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <stdint.h>
#include <alloca.h>

static void * readSP() {
  void * res;

  __asm__ volatile("movq %%rsp,%0" : "=r"(res));

  return res;
}

void checkAlloca(int Count) {
  int * Space = (int *)alloca(Count * sizeof(int));

  printf("Alloca: Space: %p, SP: %p\n", Space, readSP());
}

void checkDyanmicStackArray(int Count) {
  int Space[Count];

  printf("DynamicArray: Space: %p, SP: %p\n", &Space[0], readSP());
}

int main(int argc, char ** argv) {
  checkAlloca(argc);
  checkDyanmicStackArray(argc);
  return 0;
}
