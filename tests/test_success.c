//===-- test_success.cc - Dummy that's always successful  ---------*- C -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <stdlib.h>

int main(void) {
  int failed = 0 /* false */;
  printf("***%s***\n", failed ? "FAILED" : "PASSED");
  return failed;
}
