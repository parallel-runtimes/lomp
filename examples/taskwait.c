//===-- taskwait.c - Example for the "taskwait" construct ---------*- C -*-===//
//
// Part of the LOMP Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <unistd.h>

#include <omp.h>

void taskwait() {
#pragma omp task
  {
    printf("Task 1\n");
    sleep(1);
  }

#pragma omp task
  {
    printf("Task 2\n");
    sleep(1);
  }

#pragma omp task
  {
    printf("Task 3\n");
    sleep(1);
  }

#pragma omp task
  {
    printf("Task 4\n");
    sleep(1);
  }

#pragma omp taskwait
}

int main(void) {
#pragma omp parallel
  {
#pragma omp master
    {
#pragma omp task
      {
        printf("Task A\n");
#pragma omp task
        {
          printf("Task B\n");
#pragma omp task
          {
            printf("Task C\n");
            taskwait();
          }
        }
      }
    }
  }
  return 0;
}
