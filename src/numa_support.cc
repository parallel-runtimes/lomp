//===-- numa_suppport.cc - NUMA support functions ---------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "numa_support.h"

#include "debug.h"

#include <map>
#include <thread>

#include "target.h"

#if (LOMP_HAVE_LIBNUMA)
#include <numa.h>
#endif

#if (LOMP_TARGET_LINUX)
#include <sched.h>
#endif

#include <mutex>

namespace lomp::numa {

static const auto DebugLevel = Debug::Info;

size_t NumberOfNumaDomains;
size_t NumberOfCores;
std::vector<ArrayOfCoreIDs> CoresPerNumaDomain;
ArrayOfCoreIDs NumaDomainForCore;
std::map<Thread *, size_t> ThreadCoreMap;
std::vector<Thread *> CoreToThread;

#if !(LOMP_HAVE_LIBNUMA)
extern "C" {
void numa_error(char const * where) {
  debug(DebugLevel, "NUMA: NUMA error %s!", where);
  exit(1);
}
}
#endif

void DumpNumaDatabase() {
  auto n = 0;
  for (auto domain : CoresPerNumaDomain) {
    debug(DebugLevel, "NUMA: NUMA Domain %lu:", n);
    debugraw(DebugLevel, "NUMA:    ", domain.size(), "%d ", domain.data());
    ++n;
  }
}

void InitializeNumaSupport() {
  debug(DebugLevel, "NUMA: Initializing NUMA support.");
  bool available = false;

#if (LOMP_HAVE_LIBNUMA)
  available = numa_available() != -1;
  if (available) {
    NumberOfNumaDomains = numa_num_configured_nodes();
    NumberOfCores = numa_num_configured_cpus();
  }
  else {
    debug(DebugLevel, "NUMA: libnuma reported -1 for its API.  Defaulting to single NUMA domain.");
    NumberOfNumaDomains = 1;
    NumberOfCores = std::thread::hardware_concurrency();
  }
#else
#if (LOMP_TARGET_MACOS)
  debug(DebugLevel, "NUMA: Beware, MacOS does not support the interfaces "
                    "required to enable good NUMA support...");
#endif
  debug(DebugLevel,
        "NUMA: libnuma support was not built in. Assuming single NUMA domain!");
  NumberOfNumaDomains = 1;
  NumberOfCores = std::thread::hardware_concurrency();
#endif

  if (!available) {
    // No working linbNUMA, so pretend we have a single NUMA domain.
    auto CoresInDomain = std::vector<int>();

    for (size_t c = 0; c < NumberOfCores; ++c) {
      NumaDomainForCore.push_back(0);
      CoresInDomain.push_back(c);
    }
    CoresPerNumaDomain.push_back(CoresInDomain);

    // Resize the core-to-thread vector that we use to store stuff
    CoreToThread.resize(NumberOfCores);

    DumpNumaDatabase();
    return;
  }

  // We have a working libNUMA, so use it.
#if (LOMP_HAVE_LIBNUMA)
  debug(DebugLevel, "NUMA: Found %d core%s in %d NUMA domain%s.", NumberOfCores,
        NumberOfCores != 1 ? "s" : "", NumberOfNumaDomains,
        NumberOfNumaDomains != 1 ? "s" : "");

  struct bitmask * mask = nullptr;
  if (available) {
    mask = numa_bitmask_alloc(NumberOfCores);
  }
  // Iterate over all NUMA domains and determine the bitmask for the cores in
  // the respective NUMA domain.
  for (size_t d = 0; d < NumberOfNumaDomains; ++d) {
    auto CoresInDomain = std::vector<int>();

    if (mask && numa_node_to_cpus(d, mask)) {
      fatalError("NUMA: Error while invoking numa_node_to_cpus at %s:%d",
                 __FILE__, __LINE__);
    }

    for (int c = 0; c < NumberOfCores; ++c) {
      if (numa_bitmask_isbitset(mask, c)) {
        CoresInDomain.push_back(c);
      }
    }
    CoresPerNumaDomain.push_back(CoresInDomain);
  }

  if (mask) {
    numa_bitmask_free(mask);
  }

  // Now iterate over the cores and find their respective NUMA domain.  We
  // could be smarter and populate the second vector in the above loop, too.
  for (int c = 0; c < NumberOfCores; ++c) {
    auto result = 0;
    result = numa_node_of_cpu(c);
    if (result < 0) {
      fatalError("NUMA: Error while invoking numa_node_of_cpu at %s:%d",
                 __FILE__, __LINE__);
    }
    NumaDomainForCore.push_back(result);
  }

  // Resize the core-to-thread vector that we use to store stuff
  CoreToThread.resize(NumberOfCores);
  DumpNumaDatabase();
#endif
}

size_t GetNumberOfNumaDomains() {
  return NumberOfNumaDomains;
}

size_t GetNumberOfCores() {
  return NumberOfCores;
}

size_t GetNumaDomain(const size_t core) {
  return NumaDomainForCore.at(core);
}

const ArrayOfCoreIDs & GetCoresForNumaDomain(const int domain) {
  return CoresPerNumaDomain.at(domain);
}

std::mutex MutexRegister;
void RegisterThread(Thread * thread, size_t threadID) {
#if (LOMP_TARGET_LINUX)
  auto core = sched_getcpu();
#elif (LOMP_TARGET_MACOS)
  auto core = threadID;
#else
#error "Need thread binding and location for this OS."
#endif
  debug(DebugLevel, "NUMA: Thread %p (thread ID: %d) on core %d, domain %d",
        thread, threadID, core, GetNumaDomain(core));
  std::lock_guard<std::mutex> guard(MutexRegister);
  auto entry = std::pair<Thread *, int>{thread, core};
  ThreadCoreMap.insert(entry);
  CoreToThread[core] = thread;
}

int GetCoreForThread(Thread * thread) {
  auto element = ThreadCoreMap.find(thread);
  if (element == ThreadCoreMap.end()) {
    return -1;
  }
  return element->second;
}

void DumpThreadMap() {
  for (const auto & [thread, core] : ThreadCoreMap) {
    printf("thread %p, core %zu\n", thread, core);
  }
}

Thread * GetThreadForCore(size_t core) {
  Thread * thread = nullptr;
  if (core < CoreToThread.size()) {
    thread = CoreToThread.at(core);
  }
  return thread;
}

} // namespace lomp::numa
