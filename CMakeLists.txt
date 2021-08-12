cmake_minimum_required(VERSION 3.13.0)

project(lomp)

# General project options
enable_testing()

# By default, we want to build in Release mode
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()
message(STATUS "Build type set to: ${CMAKE_BUILD_TYPE}")


# Project languages
enable_language(CXX)
enable_language(C)

# We use C11 and C++17 and we do not want GNU extensions
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_EXTENSIONS OFF)

# set(CMAKE_VERBOSE_MAKEFILE on)

# define LOMP specific configuration options
option(LOMP_WARN_API_STUBS "Warn about missing API entry points" ON)
option(LOMP_WARN_ARCH_FEATURES "Warn about missing implementations for arch features" ON)
option(LOMP_GNU_SUPPORT "Build GNU-style runtime entry points" OFF)
option(LOMP_ICC_SUPPORT "Build additional entry points for Intel classic compilers" OFF)
option(LOMP_MICROBM_WITH_LOMP "Use LOMP for micro-benchmarks" OFF)
option(LOMP_BUILD_MICROBM "Build micro-benchmarks" ON)
option(LOMP_BUILD_EXAMPLES "Build example programs" ON)
# Assume aarch64 v8.1a architecture by default.
SET(LOMP_ARM64_ARCHITECTURE "armv8.1a" CACHE STRING "Detailed aarch64 architecture passed to -march=")
include(CheckCXXCompilerFlag)

# look for Git
find_package(Git)

# Detect unsupported compilers
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
# Apple Clang does not support OpenMP itself, but should still be able
# to build the runtime. (Just not run the tests)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Intel")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_definitions(LOMP_GCC_BUILD=1)
else()
    message(FATAL_ERROR
            "Unknown compiler: " ${CMAKE_CXX_COMPILER_ID} ".  Please use clang or Intel compilers instead.")
endif()

# detect the operating system
execute_process(COMMAND uname COMMAND tr -d '\n' OUTPUT_VARIABLE LOMP_TARGET_OS)
message(STATUS "Target system: ${LOMP_TARGET_OS}")

# Detect the machine architecture
execute_process(COMMAND arch COMMAND tr -d '\n' OUTPUT_VARIABLE LOMP_TARGET_ARCH)
message(STATUS "Architecture: ${LOMP_TARGET_ARCH}")

if (${LOMP_TARGET_ARCH} STREQUAL "aarch64")
   add_compile_options(-march=${LOMP_ARM64_ARCHITECTURE})
   message("-- LOMP_ARM64_ARCHITECTURE=${LOMP_ARM64_ARCHITECTURE} => -march=${LOMP_ARM64_ARCHITECTURE}")
   add_compile_definitions(LOMP_ARM64_ARCHITECTURE=${LOMP_ARM64_ARCHITECTURE})   
endif()

# find all LOMP source files
file(GLOB_RECURSE LOMP_SOURCE_FILES ${PROJECT_SOURCE_DIR}/*.cc
                                    ${PROJECT_SOURCE_DIR}/*.c
                                    ${PROJECT_SOURCE_DIR}/*.h)

# find clang-format and enable "format" target if found
find_program(LOMP_CLANG_FORMAT "clang-format")
if(LOMP_CLANG_FORMAT)
    message(STATUS "Found clang-format.  Enabling \"format\" target.")
    add_custom_target(format
                      COMMAND clang-format -i ${LOMP_SOURCE_FILES})
endif()

# find clang-tidy and enable "check" target if found
find_program(LOMP_CLANG_TIDY "clang-tidy")
if (LOMP_CLANG_TIDY)
    message(STATUS "Found clang-tidy.  Enabling \"check\" target.")
    add_custom_target(check
                      COMMAND clang-tidy ${LOMP_SOURCE_FILES} -- -std=c++17)
endif()

# add a custom clean target to tidy up the source tree
file(GLOB_RECURSE LOMP_EDITOR_BACKUPS ${PROJECT_SOURCE_DIR}/*.bak
                                      ${PROJECT_SOURCE_DIR}/*.~)
add_custom_target(realclean COMMAND rm -vf ${LOMP_EDITOR_BACKUPS})


if(${LOMP_TARGET_ARCH} STREQUAL "x86_64")
    # If we are building for a Intel64 machine, check for Intel TSX
    check_cxx_compiler_flag("-mrtm" LOMP_HAVE_RTM)

    # On X86_64 check for cmpxchg16b support
    check_cxx_compiler_flag("-mcx16" LOMP_HAVE_CMPXCHG16B)
endif()

# Some macros to ease dealing with compiler flags
macro(add_mrtm_flag target)
    if(LOMP_HAVE_RTM)
        target_compile_options(${target} BEFORE PRIVATE -mrtm)
        target_compile_definitions(${target} PRIVATE -DLOMP_HAVE_RTM=1)
    endif()
endmacro(add_mrtm_flag)
macro(add_mcx16_flag target)
    if(LOMP_HAVE_CMPXCHG16B)
        target_compile_options(${target} BEFORE PRIVATE -mcx16)
        target_compile_definitions(${target} PRIVATE -DLOMP_HAVE_CMPXCHG16B=1)
    endif()
endmacro(add_mcx16_flag)

# Find libatomic and and define macro to add where we need libatomic
find_library(LOMP_LIBATOMIC NAMES libatomic${LOMP_SHAREDLIB_SUFFIX}
                                  libatomic${LOMP_SHAREDLIB_SUFFIX}.1)
message(STATUS "Found libatomic: ${LOMP_LIBATOMIC}")
macro(add_libatomic target)
    if (LOMP_LIBATOMIC)
        target_compile_definitions(${target} PRIVATE LOMP_HAVE_LIBATOMIC=1)
        target_link_libraries(${target} ${LOMP_LIBATOMIC})
    endif()
endmacro(add_libatomic)

# Determine Git commit ID
if(Git_FOUND)
    execute_process (
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE LOMP_GIT_COMMIT_ID
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    set(LOMP_GIT_COMMIT_ID "unknown")
endif()
message (STATUS "LOMP build with Git commit ID: ${LOMP_GIT_COMMIT_ID}")

message(STATUS "Use LOMP for micro-benchmarks (LOMP_MICROBM_WITH_LOMP): ${LOMP_MICROBM_WITH_LOMP}")
message(STATUS "Warn about missing API entry points (LOMP_WARN_API_STUBS): ${LOMP_WARN_API_STUBS}")
message(STATUS "Warn about missing implementations for arch features (LOMP_WARN_ARCH_FEATURES): ${LOMP_WARN_ARCH_FEATURES}")
message(STATUS "Build example programs: ${LOMP_BUILD_EXAMPLES}")
message(STATUS "Build micro-benchmarks: ${LOMP_BUILD_MICROBM}")

# Add our own include directory that contains the omp.h header file
include_directories(./include)
include_directories(./src)

# Build the runtime library
add_subdirectory(src)

if(LOMP_BUILD_EXAMPLES)
    # Build the example programs
    add_subdirectory(examples)
endif()

# Build the tests
add_subdirectory(tests)

if(LOMP_BUILD_MICROBM)
    # Build the micro-benchmarks
    add_subdirectory(microBM)
endif()
