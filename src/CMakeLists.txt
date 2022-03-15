# ===------------------------------------------------------------*- CMake -*-===
#
#  Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===

# Source files for the runtime library
add_library(omp atomics.cc barrier_impl.cc entrypoints.cc environment.cc
                globals.cc locks.cc loops.cc numa_support.cc
                stats-timing.cc tasking.cc threads.cc util.cc
                target.h target_x86_64.h target_arm.h target_riscv.h
                version.h.in)
set_target_properties(omp PROPERTIES VERSION 0.1 SOVERSION 1)

# Add the current build directory as a include directory
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Add pre-processor defines
# ***BEWARE*** Do not turn debug off completely unless you also make
# some changes to the code. Some of the warning and information
# messages are printed via the internal debug code...
target_compile_definitions(omp PRIVATE DEBUG=10)
if(LOMP_ICC_SUPPORT)
    target_compile_definitions(omp PRIVATE LOMP_ICC_SUPPORT=1)
endif()
if(LOMP_GNU_SUPPORT)
    target_compile_definitions(omp PRIVATE LOMP_GNU_SUPPORT=1)
endif()
if(LOMP_WARN_API_STUBS)
    target_compile_definitions(omp PRIVATE LOMP_WARN_API_STUBS=1)
endif()
if(LOMP_WARN_ARCH_FEATURES)
    target_compile_definitions(omp PRIVATE LOMP_WARN_ARCH_FEATURES=1)
endif()

target_link_libraries(omp pthread)

# add flags for RTM and/or cmpxchg16b for X86
add_mrtm_flag(omp)
add_mcx16_flag(omp)

# Determine extension of dynamic libraries
if (${LOMP_TARGET_OS} STREQUAL "Linux")
    set(LOMP_SHAREDLIB_SUFFIX ".so")
endif()
if (${LOMP_TARGET_OS} STREQUAL "Darwin")
   set(LOMP_SHAREDLIB_SUFFIX ".dylib")
endif()
message(STATUS "Shared-library extension: ${LOMP_SHAREDLIB_SUFFIX}")

# Find libnuma and link against it if we did
find_library(LOMP_LIBNUMA libnuma${LOMP_SHAREDLIB_SUFFIX})
message(STATUS "Found libnuma: ${LOMP_LIBNUMA}")
if(LOMP_LIBNUMA)
    target_compile_definitions(omp PRIVATE LOMP_HAVE_LIBNUMA=1)
    target_link_libraries(omp ${LOMP_LIBNUMA})
endif()

# Also link with libatomic if it's available
add_libatomic(omp)

# Pass information along to know if build is building a serial LOMP version
if (LOMP_SERIAL)
    target_compile_definitions(omp PRIVATE LOMP_SERIAL=1)
endif()

# Store some metadata about the build in version.h
get_target_property(LOMP_COMPILE_OPTIONS omp COMPILE_OPTIONS)
get_target_property(LOMP_COMPILE_DEFINITIONS omp COMPILE_DEFINITIONS)
get_target_property(LOMP_VERSION omp VERSION)
get_target_property(LOMP_SOVERSION omp SOVERSION)
configure_file(version.h.in version.h)

# Enable a compilation guard that enables us to tell if we're in the RTL
target_compile_definitions(omp PRIVATE LOMP_BUILD_RTL=1)

# installation target
install(TARGETS omp)
