//===-- matmul.c - Different implementations of matrix multiplies -*- C -*-===//
//
// Part of the LOMP Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <stdlib.h>

#include <omp.h>

#define N 3072

#define DUMP_MATRIX 0

void matmul_seq(double * C, double * A, double * B, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    for (size_t k = 0; k < n; ++k) {
      for (size_t j = 0; j < n; ++j) {
        C[i * n + j] += A[i * n + k] * B[k * n + j];
      }
    }
  }
}

void matmul_par(double * C, double * A, double * B, size_t n) {
#pragma omp parallel for schedule(static, 8) firstprivate(n)
  for (size_t i = 0; i < n; ++i) {
    for (size_t k = 0; k < n; ++k) {
      for (size_t j = 0; j < n; ++j) {
        C[i * n + j] += A[i * n + k] * B[k * n + j];
      }
    }
  }
}

void init_mat(double * C, double * A, double * B, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = 0; j < n; ++j) {
      C[i * n + j] = 0.0;
      A[i * n + j] = 0.5;
      B[i * n + j] = 0.25;
    }
  }
}

void dump_mat(double * mtx, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = 0; j < n; ++j) {
      printf("%f ", mtx[i * n + j]);
    }
    printf("\n");
  }
}

double sum_mat(double * mtx, size_t n) {
  double sum = 0.0;
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = 0; j < n; ++j) {
      sum += mtx[i * n + j];
    }
  }
  return sum;
}

int main(void) {
  double ts, te;
  double t_seq;

  double * C;
  double * A;
  double * B;

  C = (double *)malloc(sizeof(*C) * N * N);
  A = (double *)malloc(sizeof(*A) * N * N);
  B = (double *)malloc(sizeof(*B) * N * N);

  init_mat(C, A, B, N);
  ts = omp_get_wtime();
  matmul_seq(C, A, B, N);
  te = omp_get_wtime();
#if DUMP_MATRIX
  dump_mat(C, N);
#endif
  t_seq = te - ts;
  printf("Sum of matrix (serial):   %f, wall time %lf, speed-up %.2lf\n",
         sum_mat(C, N), (te - ts), t_seq / (te - ts));

  init_mat(C, A, B, N);
  ts = omp_get_wtime();
  matmul_par(C, A, B, N);
  te = omp_get_wtime();
#if DUMP_MATRIX
  dump_mat(C, N);
#endif
  printf("Sum of matrix (parallel): %f, wall time %lf, speed-up %.2lf\n",
         sum_mat(C, N), (te - ts), t_seq / (te - ts));

  return EXIT_SUCCESS;
}
