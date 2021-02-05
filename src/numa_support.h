//===-- numa_suppport.h - NUMA support functions ----------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef NUMA_SUPPORT_H_INCLUDED
#define NUMA_SUPPORT_H_INCLUDED

#include <cstddef>
#include <vector>

namespace lomp {

// Forward declare thread, so that we do not need to pull in thread.h.
class Thread;

namespace numa {

typedef std::vector<int> ArrayOfCoreIDs;

void InitializeNumaSupport();
size_t GetNumberOfNumaDomains();
size_t GetNumberOfCores();
size_t GetNumaDomain(const size_t core);
const ArrayOfCoreIDs & GetCoresForNumaDomain(const int domain);
void RegisterThread(Thread *, size_t);
int GetCoreForThread(Thread * thread);
void DumpThreadMap();
Thread * GetThreadForCore(size_t core);

} // namespace numa
} // namespace lomp

#endif
