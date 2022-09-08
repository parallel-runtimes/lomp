# ===-----------------------------------------------------------*- Python -*-===
#
#  Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===

#
# Execute the scheduling  benchmark using the requested scheduling scheme at different thread counts.
#

import subprocess
import platform
import datetime
import os.path
import re
import sys
import cpuInfo

import BMutils as BMU

def main():
    BMU.extractMachineInfo()
    hostName = BMU.hostName
    # Need to be more subtle here on Apple M1, where the Python interpreter may be
    # 
    arch = BMU.arch
    threadLimit = BMU.cores
    # See if we can find out more about the machine
    modelName = BMU.modelName
    # If we can't see if it's one of the machines we know about...
    if modelName == "":
        modelName = {
            "xcil00": "Cavium ThunderX2 ARM v8.1",
            "xcil01": "Cavium ThunderX2 ARM v8.1",
            "xcimom": "Cavium ThunderX2 ARM v8.1",
            "xcimom2": "Cavium ThunderX2 ARM v8.1",
            "Js-MacBook-Pro": "Intel(R) Core(TM) i5-5257U CPU @ 2.70GHz",
        }.get(hostName, "Unknown")

    from optparse import OptionParser

    options = OptionParser()
    (options, args) = options.parse_args()

    if len(args) == 0:
        print("Need a schedule")
        sys.exit(1)
    (experiment, schedule) = args[0].split("_")

    # Parse the runtime library selection if present
    libImpl = args[1] if len(args) >= 2 else ""
    libPath = "../src/:" if "LOMP" in libImpl else ""

    # By doing this we also work around the weird MacOS behaviour (maybe security related?)
    # The DYLD_LIBRARY_PATH is *not* passed on to a grandchild process
    # by default, so we have to pass it explicitly.
    libPathName = (
        "DYLD_LIBRARY_PATH" if platform.system() == "Darwin" else "LD_LIBRARY_PATH"
    )
    currentPath = os.getenv(libPathName)
    currentPath = currentPath if currentPath != None else ""
    baseEnv = libPathName + "=" + libPath + currentPath
    baseEnv += (
        " KMP_HW_SUBSET=1T KMP_AFFINITY='compact,granularity=fine' OMP_NUM_THREADS="
    )

    image = BMU.getExecutable("scheduling")
    print("Time/cell")
    print(modelName + "," + schedule + "," + experiment + "," + libImpl)
    # print("baseEnv ", baseEnv)
    print("Cores, Samples, Min, Mean, Max, SD", flush=True)
    base = [1, 2] if threadLimit > 2 else [1]
    for t in base + list(range(4, threadLimit - 1, 4)) + [threadLimit]:
        BMU.execute(baseEnv + str(t) + " " + image + " " + experiment + "_" + schedule)


if __name__ == "__main__":
    main()
