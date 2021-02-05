# ===------------------------------------------------------------*- CMake -*-===
#
#  Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===

include(FindOpenMP)

add_compile_options(-fopenmp)

# We need to link against our runtime
link_libraries(omp)
link_libraries(m)
add_link_options(-lpthread)

# List of the tests
add_executable(test_success test_success.c)
add_executable(test_timer test_timer.cc)
add_executable(test_fork test_fork.c)
add_executable(test_parallel_reduction test_parallel_reduction.c)
add_executable(test_nthreads test_nthreads.c)
add_executable(test_master1 test_master1.c)
add_executable(test_master2 test_master2.c)
add_executable(test_single test_single.cc)
add_executable(test_locks test_locks.cc)
add_executable(test_critical test_critical.c)
add_executable(test_scheduling test_scheduling.cc)
add_executable(test_static_scheduling test_static_scheduling.cc)
add_executable(test_for_reduction test_for_reduction.c)
add_executable(test_tasks test_tasks.c)
add_executable(test_taskargs test_taskargs.c)
add_executable(test_taskwait test_taskwait.c)
add_executable(test_task_forkjoin test_task_forkjoin.c)
add_executable(test_task_barrier test_task_barrier.c)
add_executable(test_task_single test_task_single.c)

add_test(NAME success COMMAND test_success)
add_test(NAME timer COMMAND test_timer)
add_test(NAME fork COMMAND test_fork)
add_test(NAME parallel_reduction COMMAND test_parallel_reduction)
add_test(NAME nthreads COMMAND test_nthreads)
add_test(NAME master1 COMMAND test_master1)
add_test(NAME master2 COMMAND test_master2)
add_test(NAME single COMMAND test_single)
add_test(NAME locks COMMAND test_locks)
add_test(NAME critical COMMAND test_critical)
add_test(NAME static_scheduling COMMAND test_static_scheduling)
add_test(NAME scheduling COMMAND test_scheduling)
add_test(NAME for_reduction COMMAND test_for_reduction)
add_test(NAME tasks COMMAND test_tasks)
add_test(NAME task_args COMMAND test_taskargs)
add_test(NAME taskwait COMMAND test_taskwait)
add_test(NAME task_forkjoin COMMAND test_task_forkjoin)
add_test(NAME task_barrier COMMAND test_task_barrier)
add_test(NAME task_single COMMAND test_task_single)
