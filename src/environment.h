//===----------------------------------------------------------------------===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef ENVIRONMENT_H_INCLUDED
#define ENVIRONMENT_H_INCLUDED

#include <string>
#include <utility>

namespace lomp {
namespace environment {

bool getString(char const * var, std::string & value,
               const std::string & def = "");
bool getInt(char const * var, int & value, int def = 0);
bool getStringWithStringArgument(
    char const * var, std::pair<std::string, std::string> & value,
    const std::pair<std::string, std::string> & def =
        std::pair<std::string, std::string>());
bool getStringWithIntArgument(
    char const * var, std::pair<std::string, int> & value,
    const std::pair<std::string, int> & def = std::pair<std::string, int>());
;
} // namespace environment
} // namespace lomp

#endif
