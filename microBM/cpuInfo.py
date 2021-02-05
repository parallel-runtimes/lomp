# ===-----------------------------------------------------------*- Python -*-===
#
#  Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===

import re


def getCpuInfo(field):
    """Read a given field from /proc/cpuinfo
    Returns the first line which has the relevant fieldname
    Opens and reads the file once for each enquiry, but this is mostly for
    headers and so on, so it's probably not worth worrying about!
    """
    # Unfortunately /proc/cpuinfo on the aarch64 machines is rather unhelpful
    # it has none of the interesting fields below :-(
    # Which makes this less useful than I had hoped.
    regexp = "^" + field + "\s+: *(.*)$"
    lineMatch = re.compile(regexp)

    try:
        with open("/proc/cpuinfo", "r") as cpuInfo:
            for line in cpuInfo:
                match = lineMatch.match(line)
                if match:
                    return match.group(1).strip()
    except:
        return ""
    return ""


if __name__ == "__main__":
    for field in ("model name", "cpu cores", "vendor_id"):
        print('getCpuInfo("' + field + '") = ' + getCpuInfo(field))
