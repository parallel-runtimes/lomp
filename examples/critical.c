#include <stdio.h>
#include <stdlib.h>

#include <omp.h>

void critical_example() {
#pragma omp critical
  { printf("Hello World from thread %d!\n", omp_get_thread_num()); }
}

int main(void) {
#pragma omp parallel
  { critical_example(); }

  return EXIT_SUCCESS;
}