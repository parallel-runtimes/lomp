//===-- direct_task.c - Task example with code outlining shown ----*- C -*-===//
//
// Part of the LOMP Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <omp.h>

#define NTASKS 16

/* function prototypes of the runtime */
typedef int32_t (*thunk_ptr_t)(int32_t, void *);
void * __kmpc_omp_task_alloc(void *, int32_t, void *, size_t, size_t,
                             thunk_ptr_t);
int32_t __kmpc_omp_task(void *, int32_t, void *);

void produce_original(double d) {
  for (int i = 0; i < NTASKS; i++) {
    // create a new task to for another thread to steal
    printf("%d: creating task\n", omp_get_thread_num());

#pragma omp task firstprivate(i) firstprivate(d)
    {
      double answer = i * d;
      printf("%d: Hello from task %d and the answer is %lf\n",
             omp_get_thread_num(), i, answer);
    }

    // slow down a little in producing the tasks, so that a worker
    // can steal the task from the queue
    sleep(1);
  }
}

int32_t __omp_produce_thunk_0(int32_t gtid, void * task) {
  (void)gtid;
  char * data = ((char **)task)[0];
  int i = *((int *)data);
  double d = *((double *)(data + 8));
  double answer = i * d;
  printf("%d: Hello from task %d and the answer is %lf\n", omp_get_thread_num(),
         i, answer);
  return 0;
}

void produce_transformed(double d) {
  for (int i = 0; i < NTASKS; i++) {
    // create a new task to for another thread to steal
    printf("%d: creating task\n", omp_get_thread_num());

    void * task = __kmpc_omp_task_alloc(NULL, 0, NULL, 40 + 16, 16,
                                        __omp_produce_thunk_0);
    char * data = ((char **)task)[0];
    *((int *)data) = i;
    *((double *)(data + 8)) = d;
    __kmpc_omp_task(NULL, 0, task);

    // slow down a little in producing the tasks, so that a worker
    // can steal the task from the queue
    sleep(1);
  }
}

int32_t __omp_produce_thunk_0_memcpy(int32_t gtid, void * task) {
  (void)gtid;
  char * data = ((char **)task)[0];
  int i;
  double d;
  memcpy(&i, data + 0, sizeof(int));
  memcpy(&d, data + 8, sizeof(double));
  double answer = i * d;
  printf("%d: Hello from task %d and the answer is %lf\n", omp_get_thread_num(),
         i, answer);
  return 0;
}

void produce_transformed_memcpy(double d) {
  for (int i = 0; i < NTASKS; i++) {
    // create a new task to for another thread to steal
    printf("%d: creating task\n", omp_get_thread_num());

    void * task = __kmpc_omp_task_alloc(NULL, 0, NULL, 40 + 16, 16,
                                        __omp_produce_thunk_0_memcpy);
    char * data = ((char **)task)[0];
    memcpy(data + 0, &i, sizeof(int));
    memcpy(data + 8, &d, sizeof(double));
    __kmpc_omp_task(NULL, 0, task);

    // slow down a little in producing the tasks, so that a worker
    // can steal the task from the queue
    sleep(1);
  }
}

void consume() {
  // This function is only a placeholder to mark a worker thread
  // as being a consumer.  The actual task execution is done in
  // the barrier of the parallel region.
}

int main(void) {
  double d = 42.0;
#pragma omp parallel
  {
    if (omp_get_thread_num() == 0) {
      produce_transformed_memcpy(d);
    }
    else {
      consume();
    }
  }
  return 0;
}
