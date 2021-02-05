//===------------------------------------------------------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef OMP_H_INCLUDED
#define OMP_H_INCLUDED

// Could be smarter here and allow some locks which do not need a pointer,
// however this is easier for now.
/* lock API functions */
typedef struct omp_lock_t {
  void * _lk;
} omp_lock_t;

/* nested lock API functions */
typedef struct omp_nest_lock_t {
  void * _lk;
} omp_nest_lock_t;

/* OpenMP 5.0  Synchronization hints*/
typedef enum omp_sync_hint_t {
  omp_sync_hint_none = 0,
  omp_lock_hint_none = omp_sync_hint_none,
  omp_sync_hint_uncontended = 1,
  omp_lock_hint_uncontended = omp_sync_hint_uncontended,
  omp_sync_hint_contended = (1 << 1),
  omp_lock_hint_contended = omp_sync_hint_contended,
  omp_sync_hint_nonspeculative = (1 << 2),
  omp_lock_hint_nonspeculative = omp_sync_hint_nonspeculative,
  omp_sync_hint_speculative = (1 << 3),
  omp_lock_hint_speculative = omp_sync_hint_speculative,
} omp_sync_hint_t;

/* schedule kind constants */
typedef enum omp_sched_t {
  omp_sched_static = 1,
  omp_sched_dynamic = 2,
  omp_sched_guided = 3,
  omp_sched_auto = 4,
  lomp_sched_imbalanced =
      32, // For testing purposes only. Uses static steal scheduling but allocates all the work initially to one thread
  // to force lots of competing stealing to be going on.
  omp_sched_monotonic = 0x80000000
} omp_sched_t;

/* lock hint type for dynamic user lock */
typedef omp_sync_hint_t omp_lock_hint_t;

#if (__cplusplus)
extern "C" {
#endif
// Environment enquiry.
extern int omp_get_thread_num(void);
extern int omp_get_num_threads(void);
extern int omp_get_max_threads(void);
extern int omp_in_parallel(void);

// Attempt to change the number of threads.
// N.B in LOMP at present this can only be set before any parallel region, or set to what it already is at present!
extern void omp_set_num_threads(int);

// Lock functions
extern void omp_init_lock(omp_lock_t *);
extern void omp_init_lock_with_hint(omp_lock_t *, omp_lock_hint_t);
extern void omp_set_lock(omp_lock_t *);
extern void omp_unset_lock(omp_lock_t *);
extern void omp_destroy_lock(omp_lock_t *);
extern int omp_test_lock(omp_lock_t *);
#if (0)
/* Not yet implemented */
extern void omp_init_nest_lock(omp_nest_lock_t *);
extern void omp_init_nest_lock_with_hint(omp_nest_lock_t *, omp_lock_hint_t);
extern void omp_set_nest_lock(omp_nest_lock_t *);
extern void omp_unset_nest_lock(omp_nest_lock_t *);
extern void omp_destroy_nest_lock(omp_nest_lock_t *);
extern int omp_test_nest_lock(omp_nest_lock_t *);
#endif

// Control for schedule(runtime) interface
extern void omp_set_schedule(omp_sched_t, int);
extern void omp_get_schedule(omp_sched_t *, int *);

// Timing Routine
extern double omp_get_wtime(void);

// Much more to come: enumerations for various things, ...
#if (__cplusplus)
}
#endif

#endif
