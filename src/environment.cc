//===----------------------------------------------------------------------===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

/** @file environment.cc
 * Functions to deal with environment variables and provide simple parsers
 * to decode the OpenMP ICV settings coming from the environment variables.
 */

#include "environment.h"

#include <cstdlib>

#include "debug.h"

namespace lomp::environment {

bool getString(char const * var, std::string & value, const std::string & def) {
  debug_enter();
  const auto * str = std::getenv(var);
  value = str ? std::string(str) : def;
  debug_leave();
  return str != nullptr;
}

bool getInt(char const * var, int & value, int def) {
  debug_enter();
  std::string str;

  if (!getString(var, str)) {
    value = def;
    debug_leave();
    return false;
  }

  try {
    value = stoi(str);
    debug_leave();
    return true;
  } catch (...) {
    value = def;
    debug_leave();
    return false;
  }
}

bool getStringWithStringArgument(
    char const * var, std::pair<std::string, std::string> & value,
    const std::pair<std::string, std::string> & def) {
  debug_enter();
  std::string str;
  if (!getString(var, str)) {
    value = def;
    debug_leave();
    return false;
  }

  std::size_t pos = str.find(',');
  value = std::make_pair(str.substr(0, pos), str.substr(pos + 1, str.length()));

  debug_leave();
  return true;
}

bool getStringWithIntArgument(char const * var,
                              std::pair<std::string, int> & value,
                              const std::pair<std::string, int> & def) {
  debug_enter();
  std::pair<std::string, std::string> tmp;
  bool result = false;

  if (!getStringWithStringArgument(var, tmp)) {
    value = def;
    debug_leave();
    return false;
  }

  try {
    std::string str = tmp.first;
    int num = stoi(tmp.second);
    value = std::make_pair(str, num);
    result = true;
  } catch (...) {
    value = def;
    result = false;
  }

  debug_leave();
  return result;
}

} // namespace lomp::environment
