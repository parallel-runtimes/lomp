//===-- thunk.h - Definitions of the thunk pointer types --------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef THUNK_H_INCLUDED
#define THUNK_H_INCLUDED

#include "target.h"

namespace lomp {

// The outlined parallel region body created by the compiler and passed to __kmpc_fork_call
#if (MAX_REGISTER_ARGS == 6)
typedef void (*BodyTypeLLVM)(void * GTid, void * LTid, void *, void *, void *,
                             void *);
#elif (MAX_REGISTER_ARGS == 8)
typedef void (*BodyTypeLLVM)(void * GTid, void * LTid, void *, void *, void *,
                             void *, void *, void *);
#else
#error MAX_REGISTER_ARGS seems not to be defined.
#endif

#if (LOMP_GNU_SUPPORT)
typedef void (*BodyTypeGNU)(void *);
#endif

} // namespace lomp

#endif