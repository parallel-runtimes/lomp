//===-- locks.h - lock function definitions ---------------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the definitions assocuiayed with lock functions which
/// are needed by the rest of the runtime. The user-level interfaces are defined in
/// interface.h
///
//===----------------------------------------------------------------------===//
#ifndef LOCKS_H_INCLUDED
#define LOCKS_H_INCLUDED

namespace lomp::locks {

// Standard lock support
void InitializeLocks();
void InitLock(omp_lock_t * lock);
void DestroyLock(omp_lock_t * lock);
void SetLock(omp_lock_t * lock);
void UnsetLock(omp_lock_t * lock);
int TestLock(omp_lock_t * lock);

// Support for "critical" construct
void InitLockCritical(omp_lock_t * lock);
void EnterCritical(omp_lock_t * lock);
void LeaveCritical(omp_lock_t * lock);

} // namespace lomp::locks

#endif
