//
// Created by micha on 19.02.2022.
//

#ifndef LOMP_MEMORY_H
#define LOMP_MEMORY_H

#include "target.h"

#include <new>

namespace lomp::memory {

template<typename AllocType>
AllocType * aligned_alloc(size_t alignment = CACHELINE_SIZE) {
  return new (std::align_val_t(alignment)) AllocType{};
}

template<typename AllocType>
AllocType * aligned_alloc_array(size_t size, size_t alignment = CACHELINE_SIZE) {
  printf("in %s\n", __FUNCTION__ );
  return new (std::align_val_t(alignment)) AllocType[size];
}

void * aligned_alloc_chunk(size_t size, size_t alignment = CACHELINE_SIZE) {
  printf("in %s\n", __FUNCTION__ );
  return new (std::align_val_t(alignment)) char[size];
}

template<typename AllocType>
AllocType * aligned_free(AllocType * ptr) {
  printf("in %s\n", __FUNCTION__ );
  delete ptr;
}

template<typename AllocType>
AllocType * aligned_free_array(AllocType * ptr) {
  printf("in %s\n", __FUNCTION__ );
  delete[] ptr;
}

void aligned_free_chunk(void * ptr) {
  printf("in %s\n", __FUNCTION__ );
  auto * chunk =reinterpret_cast<char *>(ptr);
  delete[] chunk;
}

}

#endif //LOMP_MEMORY_H
