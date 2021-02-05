//===-- life_tasks.cc - Example creating some tasks ---------------*- C -*-===//
//
// Part of the LOMP Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <cstring>

#include <stdio.h>
#include <unistd.h>

#include <omp.h>

template <typename F>
void __omp_create_task(F f) {
  f();
}

extern "C" {
/* function prototypes of the runtime */
typedef int32_t (*thunk_ptr_t)(int32_t, void *);
void * __kmpc_omp_task_alloc(void *, int32_t, void *, size_t, size_t,
                             thunk_ptr_t);
int32_t __kmpc_omp_task(void *, int32_t, void *);
}

// before task outlining:
void life() {
  float value = 21.0f;
  int factor = 2;
  int thread_id = -1;
#pragma omp parallel
#pragma omp master
  {
#pragma omp task shared(value) firstprivate(factor) private(thread_id)
    {
      thread_id = omp_get_thread_num();
      std::cout << thread_id << " says: The meaning of life is "
                << (value * factor) << std::endl;
    }
  }
}

// after task outlining:
int32_t __omp_life_thunk_0_transformed(int32_t gtid, void * task) {
  char * data = ((char **)task)[0];
  float * value;
  int factor;
  int thread_id;
  memcpy(&value, data + 0, sizeof(float *));
  memcpy(&factor, data + 8, sizeof(int));
  thread_id = omp_get_thread_num();
  std::cout << thread_id << " says: The meaning of life is "
            << (*value * factor) << std::endl;
  std::flush(std::cout);
  return 0;
}

void life_transformed() {
  float value = 21.0f;
  int factor = 2;
  int thread_id = -1;
#pragma omp parallel
#pragma omp master
  {
    void * task = __kmpc_omp_task_alloc(NULL, 0, NULL, 40 + 16, 16,
                                        __omp_life_thunk_0_transformed);
    char * data = ((char **)task)[0];
    float * value_ptr = &value;
    std::memcpy(data + 0, &value_ptr, sizeof(float *));
    std::memcpy(data + 8, &factor, sizeof(int));
    __kmpc_omp_task(NULL, 0, task);
    // sleep(1);
  }
}

int main(void) {
  life_transformed();
  return 0;
}
