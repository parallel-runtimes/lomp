#===------------------------------------------------------------*- CMake -*-===
#
#  Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===

include(FindOpenMP)
include(FindPython3)

add_compile_options(-fopenmp)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

if(LOMP_ICC_SUPPORT)
    add_compile_definitions(LOMP_ICC_SUPPORT=1)
endif()
if(LOMP_GNU_SUPPORT)
    add_compile_definitions(LOMP_GNU_SUPPORT=1)
endif()
if(LOMP_WARN_ARCH_FEATURES)
    target_compile_definitions(omp PRIVATE LOMP_WARN_ARCH_FEATURES=1)
endif()

# List of the example codes and their respective input and special options
add_executable(atomics atomics.cc ../src/stats-timing.cc ../src/util.cc)
if(LOMP_MICROBM_WITH_LOMP)
  target_link_libraries(atomics omp)
else()
  target_link_options(atomics PRIVATE -fopenmp)
endif()

add_executable(barriers barriers.cc ../src/barrier_impl.cc ../src/stats-timing.cc ../src/util.cc)
if(LOMP_MICROBM_WITH_LOMP)
  target_link_libraries(barriers omp)
else()
  target_link_options(barriers PRIVATE -fopenmp)
endif()

add_executable(checkRandom checkRandom.cc ../src/stats-timing.cc ../src/util.cc)
if(LOMP_MICROBM_WITH_LOMP)
  target_link_libraries(checkRandom omp)
else()
  target_link_options(checkRandom PRIVATE -fopenmp)
endif()

if(CMAKE_SYSTEM_NAME EQUAL Linux)
  # Linux specific tests...
  # These two measure the performance of the futex system call which is
  # only available on Linux
  add_executable(cvwake cvwake.cc ../src/stats-timing.cc ../src/util.cc)
  if(LOMP_MICROBM_WITH_LOMP)
    target_link_libraries(cvwake omp)
  else()
    target_link_options(cvwake PRIVATE -fopenmp)
  endif()

  add_executable(futex futex.cc ../src/stats-timing.cc ../src/util.cc)
  if(LOMP_MICROBM_WITH_LOMP)
    target_link_libraries(futex omp)
  else()
    target_link_options(futex PRIVATE -fopenmp)
  endif()
endif()

add_custom_command(OUTPUT rawLoadsStores.h
 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generateRandomAccess.py > ${CMAKE_CURRENT_BINARY_DIR}/rawLoadsStores.h
 DEPENDS generateRandomAccess.py
 COMMENT "Generating rawLoadsStores.h"
 )
add_executable(loadsStores loadsStores.cc ../src/stats-timing.cc ../src/util.cc ${CMAKE_CURRENT_BINARY_DIR}/rawLoadsStores.h)
target_include_directories(loadsStores PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
if(LOMP_MICROBM_WITH_LOMP)
  target_link_libraries(loadsStores omp)
else()
  target_link_options(loadsStores PRIVATE -fopenmp)
endif()

add_executable(locks locks.cc ../src/stats-timing.cc ../src/util.cc)
if(LOMP_MICROBM_WITH_LOMP)
  target_link_libraries(locks omp)
else()
  target_link_options(locks PRIVATE -fopenmp)
endif()

add_executable(nanosleep nanosleep.cc ../src/stats-timing.cc ../src/util.cc)
if(LOMP_MICROBM_WITH_LOMP)
  target_link_libraries(nanosleep omp)
else()
  target_link_options(nanosleep PRIVATE -fopenmp)
endif()

add_executable(nominalFrequency nominalFrequency.cc ../src/stats-timing.cc ../src/util.cc)
if(LOMP_MICROBM_WITH_LOMP)
  target_link_libraries(nominalFrequency omp)
else()
  target_link_options(nominalFrequency PRIVATE -fopenmp)
endif()

add_executable(pthreadSpawn pthreadSpawn.cc ../src/stats-timing.cc ../src/util.cc)
if(LOMP_MICROBM_WITH_LOMP)
  target_link_libraries(pthreadSpawn omp)
else()
  target_link_options(pthreadSpawn PRIVATE -fopenmp)
endif()

add_executable(scheduling scheduling.cc ../src/stats-timing.cc ../src/util.cc)
if(LOMP_MICROBM_WITH_LOMP)
  target_link_libraries(scheduling omp)
else()
  target_link_options(scheduling PRIVATE -fopenmp)
endif()

add_executable(syscall syscall.cc ../src/stats-timing.cc ../src/util.cc)
if(LOMP_MICROBM_WITH_LOMP)
  target_link_libraries(syscall omp)
else()
  target_link_options(syscall PRIVATE -fopenmp)
endif()

add_executable(trivial trivial.cc ../src/stats-timing.cc ../src/util.cc)
if(LOMP_MICROBM_WITH_LOMP)
  target_link_libraries(trivial omp)
else()
  target_link_options(trivial PRIVATE -fopenmp)
endif()

add_executable(test_tracer test_tracer.cc)
if(LOMP_MICROBM_WITH_LOMP)
  target_link_libraries(test_tracer omp)
else()
  target_link_options(test_tracer PRIVATE -fopenmp)
endif()

add_executable(threadSpawnCXX threadSpawnCXX.cc ../src/stats-timing.cc ../src/util.cc)
if(LOMP_MICROBM_WITH_LOMP)
  target_link_libraries(threadSpawnCXX omp)
else()
  target_link_options(threadSpawnCXX PRIVATE -fopenmp)
endif()

add_executable(yield yield.cc ../src/stats-timing.cc ../src/util.cc)
if(LOMP_MICROBM_WITH_LOMP)
  target_link_libraries(yield omp)
else()
  target_link_options(yield PRIVATE -fopenmp)
endif()

add_executable(variadic variadic.cc ../src/stats-timing.cc ../src/util.cc)
if(LOMP_MICROBM_WITH_LOMP)
  target_link_libraries(nominalFrequency omp)
else()
  target_link_options(nominalFrequency PRIVATE -fopenmp)
endif()

# add flags for RTM and/or cmpxchg16b
add_mrtm_flag(locks)
add_mcx16_flag(locks)
