# ===------------------------------------------------------------*- CMake -*-===
#
#  Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===

include(FindOpenMP)

add_compile_options(-fopenmp)
include_directories(../include)

# We need to link against our runtime
link_libraries(omp)
add_link_options(-lpthread)

# List of the example codes and their respective input
add_executable(direct_task direct_task.c)
add_executable(fib fib.c)
add_executable(hello_world hello_world.c)
add_executable(locks_example locks_example.c)
add_executable(life life.c)
add_executable(matmul matmul.c)
add_executable(one_task one_task.c)
add_executable(single_producer single_producer.c)
add_executable(task_barrier task_barrier.c)
add_executable(taskgroup taskgroup.c)
add_executable(task_hello task_hello.c)
add_executable(taskwait taskwait.c)
add_executable(critical critical.c)

# Use C99 by default
string(APPEND CMAKE_C_FLAGS " -std=c99")
