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

#include <new>

namespace lomp::memory {

template<typename AllocType, typename... Args>
inline AllocType * make_aligned(Args&&... args) {
  fprintf(stderr, "called %s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
  return new (std::align_val_t(CACHELINE_SIZE)) AllocType(std::forward<Args>(args)...);
}

template<typename AllocType, typename... Args>
inline AllocType * make_aligned_struct(Args&&... args) {
  fprintf(stderr, "called %s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
  return new (std::align_val_t(CACHELINE_SIZE)) AllocType{std::forward<Args>(args)...};
}

inline void * make_aligned_chunk(size_t size) {
  fprintf(stderr, "called %s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
  return new (std::align_val_t(CACHELINE_SIZE)) char[size];
}

template<typename AllocType>
inline void delete_aligned(AllocType * ptr) {
  fprintf(stderr, "called %s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
  delete ptr;
}

template<typename AllocType>
inline void delete_aligned_struct(AllocType * ptr) {
  fprintf(stderr, "called %s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
  delete ptr;
}

inline void delete_aligned_chunk(void * ptr) {
  fprintf(stderr, "called %s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
  auto * chunk = reinterpret_cast<char *>(ptr);
  delete[] chunk;
}

struct CacheAligned {
    void * operator new(std::size_t sz) {
      fprintf(stderr, "new at %s:%d\n", __FILE__, __LINE__);
      return make_aligned_chunk(sz);
    }

    void operator delete(void * ptr) {
      fprintf(stderr, "delete at %s:%d\n", __FILE__, __LINE__);
      delete_aligned_chunk(ptr);
    }
};

} // namespace lomp::memory

#endif //LOMP_MEMORY_H
