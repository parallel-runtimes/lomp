//===-- memory.h - LOMP Memory Allocation Interfaces ------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LOMP_MEMORY_H
#define LOMP_MEMORY_H

#include "target.h"
#include "debug.h"

#include <new>

namespace lomp::memory {

template<typename AllocType, typename... Args>
inline AllocType * make_aligned(Args&&... args) {
  auto * ptr = new (std::align_val_t(CACHELINE_SIZE)) AllocType(std::forward<Args>(args)...);
  debug(Debug::MemoryAllocation,
        "aligned allocation of %zu bytes at %p via %s",
        sizeof(AllocType), ptr, __FUNCTION__);
  return ptr;
}

template<typename AllocType, typename... Args>
inline AllocType * make_aligned_struct(Args&&... args) {
  auto * ptr = new (std::align_val_t(CACHELINE_SIZE)) AllocType{std::forward<Args>(args)...};
  debug(Debug::MemoryAllocation,
        "aligned allocation of %zu bytes at %p via %s",
        sizeof(AllocType), ptr, __FUNCTION__);
  return ptr;
}

inline void * make_aligned_chunk(size_t size) {
  auto * ptr = new (std::align_val_t(CACHELINE_SIZE)) char[size];
  debug(Debug::MemoryAllocation,
        "aligned allocation of %zu bytes at %p via %s",
        size, ptr, __FUNCTION__);
  return ptr;
}

template<typename AllocType>
inline void delete_aligned(AllocType * ptr) {
  debug(Debug::MemoryAllocation,
        "deallocation of pointer %p via %s()",
        ptr, __FUNCTION__);
  delete ptr;
}

template<typename AllocType>
inline void delete_aligned_struct(AllocType * ptr) {
  debug(Debug::MemoryAllocation,
        "deallocation of pointer %p via %s()",
        ptr, __FUNCTION__);
  delete ptr;
}

inline void delete_aligned_chunk(void * ptr) {
  debug(Debug::MemoryAllocation,
        "deallocation of pointer %p via %s()",
        ptr, __FUNCTION__);
  auto * chunk = reinterpret_cast<char *>(ptr);
  delete[] chunk;
}

struct CacheAligned {
    static const auto alignment = CACHELINE_SIZE;

    void * operator new(std::size_t sz) {
      return make_aligned_chunk(sz);
    }

    void operator delete(void * ptr) {
      delete_aligned_chunk(ptr);
    }
};

} // namespace lomp::memory

#endif //LOMP_MEMORY_H
