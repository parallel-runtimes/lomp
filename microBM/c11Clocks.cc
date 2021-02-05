//===------------------------------------------------------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <chrono>
#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>
using namespace std;

// Code from http://blog.metaclassofnil.com/?p=323

template <typename C>
void print_clock_info(const char * name, const C & c) {
  typename C::duration unit(1);
  typedef typename C::period period;
  cout << "Clock info for " << name << ":\n"
       << "period: " << period::num * 1000000000ull / period::den << " ns \n"
       << "unit: " << chrono::duration_cast<chrono::nanoseconds>(unit).count()
       << " ns \n"
       << "Steady: " << (c.is_steady ? "true" : "false") << "\n\n";
}

int main(int argc, char ** argv) {
  chrono::high_resolution_clock highc;
  chrono::steady_clock steadyc;
  chrono::system_clock sysc;

  print_clock_info("High Resolution Clock", highc);
  print_clock_info("Steady Clock", steadyc);
  print_clock_info("System Clock", sysc);

  const long long iters = 10000000;

  vector<long long> vec(iters);
  auto ref_start = highc.now();
  for (int i = 0; i < iters; ++i) {
    vec[i] = i;
  }
  cout << "Time/iter, no clock: "
       << chrono::duration_cast<chrono::nanoseconds>(highc.now() - ref_start)
                  .count() /
              iters
       << " ns\n";

  auto start = highc.now();
  for (int i = 0; i < iters; ++i) {
    auto time =
        chrono::duration_cast<chrono::nanoseconds>(highc.now() - start).count();
    vec[i] = time;
  }
  cout << "Time/iter, clock: "
       << chrono::duration_cast<chrono::nanoseconds>(highc.now() - start)
                  .count() /
              iters
       << " ns\n";

  auto end = unique(vec.begin(), vec.end());
  adjacent_difference(vec.begin(), end, vec.begin());
  auto min = *min_element(vec.begin() + 1, end);
  cout << "Min time delta: " << min << " ns\n";
}
