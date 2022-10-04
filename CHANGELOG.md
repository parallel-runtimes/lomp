# Version 0.3
* Feature: Implement `OMP_DISPLAY_ENV` and `omp_display_env()`
* Feature: Implement a serial version of LOMP runtime.
* Feature: Support static-library version of LOMP
* Internal: use aligned allocations instead of regular malloc/free & new/delete
* Internal: added a configuration option if a max of 256 threads is not sufficient

# Version 0.2
* Feature: Add entrypoints for tasks with `if(0)` clause.
* Feature: Update the micro-benchmarks to allow measurement with each core
  active in the loadsStores benchmark, and allocate the relevant
  memory in the active thread.
* Feature: Add the ability to read multiple experiments from a single file, and
  to plot heatmaps to plot.py
* Feature: Improve the detection of some Arm architecture cores.
* Feature: provide target to install LOMP
* Documentation: added `critical` construct to README.md

# Version 0.1
* Initial release of the LOMP runtime library
* Feature: support the `parallel` construct, including the data-sharing
  clauses `shared`,  `private`, `firstprivate`, and `lastprivate`.
* Feature: Support the `master` and `single` constructs.
* Feature: Support the `barrier` construct.
* Feature: Initial support reductions (via `critical`, no tree reductions).
* Feature: Support for worksharing constructs `for` (C/C++) and `do` (Fortran).
* Feature: Support for scheduling types `static`, `dynamic`, `auto` , and
  `guided`, along with the additional `monotonic` and `nonmonotonic`
  qualifiers.
* Feature: Support for the `flush` construct.
* Feature: Support for the `task` construct, including the data-sharing clauses
  `shared`, `private`, and `firstprivate`.
* Feature: Support for the `taskwait` construct and the `taskgroup` construct.
* Feature: Support for lock functions of the OpenMP API.
* Feature: Support for execution environment routines `omp_get_thread_num()`,
  `omp_get_num_threads()`, `omp_set_num_threads()`, `omp_get_max_threads()`
  and `omp_in_parallel()`.
* Feature: Support for `omp_set_schedule()` and `omp_get_schedule()`.
* Feature: Support for `omp_get_wtime()`.
