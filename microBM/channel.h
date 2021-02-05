//===-- channel.h - A point to point synchronized channel -------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains code for a point to point synchronised channel.
/// It is used in the micro-benhcmarks for synchronization, and performance
/// measurement.

#ifndef CHANNEL_H_INCLUDED
#define CHANNEL_H_INCLUDED

#include "target.h"

template <class payload, bool doAtomic>
class channelImpl {
  CACHE_ALIGNED std::atomic<bool> go;
  payload PayLoad;

public:
  void init() {
    go = false;
  }
  channelImpl() {
    init();
  }

  void waitFor(bool required) const {
    /* Wait for state to be that required */
    while (go.load(std::memory_order_acquire) != required) {
      Target::Yield();
    }
  }
  // This one doesn't check that the previous value has been consumed.
  // In general, that is unsafe and incorrect, but inside a barrier
  // we know that the thread must have seen the previous value because
  // it checkd in again before we got here, so it's OK there.
  void unsafeRelease() {
    if (doAtomic) {
      go = !go;
    }
    else {
      go.store(true, std::memory_order_release);
    }
  }

  void release() {
    waitFor(false);
    unsafeRelease();
  }

  void send(payload const & data) {
    waitFor(false);
    PayLoad = data;
    unsafeRelease();
  }
  void unsafeSend(payload const & data) {
    PayLoad = data;
    unsafeRelease();
  }

  void wait() {
    waitFor(true);
    if (doAtomic) {
      go = !go;
    }
    else {
      go.store(false, std::memory_order_release);
    }
  }
  payload recv() {
    waitFor(true);
    payload result = PayLoad;
    if (doAtomic) {
      go = !go;
    }
    else {
      go.store(false, std::memory_order_release);
    }
    return result;
  }
  bool isReady() const {
    return go.load(std::memory_order_acquire);
  }
};

class noLoad {
public:
  noLoad() {}
  ~noLoad() {}
  void operator=(noLoad &) {}
};

template <class payload>
class channel : public channelImpl<payload, false> {};

template <bool doAtomic>
class syncOnlyChannelImpl : public channelImpl<noLoad, doAtomic> {};

typedef syncOnlyChannelImpl<false> syncOnlyChannel;
typedef syncOnlyChannelImpl<true> atomicSyncOnlyChannel;

#endif
