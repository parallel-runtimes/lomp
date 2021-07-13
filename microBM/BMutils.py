# ===-----------------------------------------------------------*- Python -*-===
#
#  Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===

#
# Util functions for running benchmarks.
#
import subprocess
import platform
import datetime
import os.path
import re
import sys
import cpuInfo

hostName = platform.node().split(".")[0]
modelName = ""
arch  = ""
threadsPerCore = 1
cores = 1

def capture(cmd):
    try:
        return subprocess.run(cmd, stdout=subprocess.PIPE,shell=True).stdout.decode("utf-8")
    except:
        return ""
    
def extractMachineInfo():
    """Try to find out useful information about the machine.
    This seems harder than is reasonable! /proc/cpuinfo on Arm machines
    is not as useful as it might be.
    """
    global arch, cores, threadsPerCore, modelName

    if platform.system() == "Darwin":
        # Extract info from sysctl
        # On Apple we have to be careful because on the M1 machines
        # they also emulate X86_64, so what we see here from Python's
        # platform module may reflect the Python interpreter was built,
        # not the way the benchmark was!
        # Since we know that our code inside the benchmarks can work it
        # out, we don't actually do it here.
        # if int(capture("sysctl -n hw.optional.arm64")):
        #    arch = "aarch64"
        cores = int(capture("sysctl -n hw.physicalcpu"))
        threads = int(capture("sysctl -n hw.logicalcpu"))
        threadsPerCore = threads//cores
        # Similarly for the model name.
        # modelName = capture("sysctl -n machdep.cpu.brand_string").strip()
        return

    if modelName == "":
        modelName = cpuInfo.getCpuInfo("model name")

    # Linux...
    # Not as good as sysctl, since it doesn't easily distinguish SMT threads
    # and "real" CPUs.
    cores = os.cpu_count()
    # Try to extract more info from lscpu
    lscpu = capture("lscpu")
    threadsPerCore = 0
    for line in lscpu.split("\n"):
        if "Thread(s) per core:" in line:
            threadsPerCore = int(line.split(":")[1])
            break
        if modelName == "" and "Model name:" in line:
            modelName = line.split(":")[1].strip()
    if threadsPerCore == 0:
        # Dubious in the extreme
        threadsPerCore = {"aarch64" : 4, "x86_64" : 2}[arch]
    cores = cores//threadsPerCore

    # If we can't, then see if it's one of the machines we know about...
    if modelName == "":
        # Host name to model name. Very installation dependent.
        # Fixes needed here for other environments
        hostName = platform.node().split(".")[0]
        knownHostTags = { "xcil": "Marvell ThunderX2 ARM v8.1",
                          "a64fx": "Fujitsu A64FX"}
        for k in knownHostTags.keys():
            if hostName in k:
                modelName = knownHostTags[k]
                break
    if modelName == "":
        modelName = "UNKOWN CPU"
        
# Functions which may be useful elsewhere
def outputName(test):
    """Generate an output file name based on the test, hostname, date, and a sequence number"""
    dateString = datetime.date.today().isoformat()
    nameBase = test + "_" + hostName + "_" + dateString
    nameBase = nameBase.replace("__", "_")
    fname = nameBase + "_1.res"
    version = 1
    while os.path.exists(fname):
        version = version + 1
        fname = nameBase + "_" + str(version) + ".res"
    return fname


def execute(cmd, output = None):
    if output:
        output = " > " + output
        print("Running " + cmd + output)
    else:
        output = ""
    subprocess.run(cmd + output, shell=True)


def getExecutable(image):
# Not needed with CMAKE, since we now assume that there's a single
# build directory for each target, and that this is executed from
# that directory
    return "./" + image
    
def computeEnv():
    extractMachineInfo()
    print ("Arch (may be wrong!):", arch,"\nModel:",modelName,"\nCores: ", cores)
    
    env = ("TARGET_MACHINE='" + modelName + "' ") if modelName else ""
    env += "OMP_NUM_THREADS=" + str(cores)
    env += " KMP_HW_SUBSET=1T KMP_AFFINITY='compact,granularity=fine' "
    return env


class runDescription:
    def __init__(self, image, subOpts, extraOpts, prefix):
        self.image = image
        self.subOptions = subOpts
        self.extraOptions = extraOpts
        self.outputNamePrefix = prefix

    def __str__(self):
        return (
            "Image: "
            + self.image
            + "\nprefix: "
            + self.outputNamePrefix
            + "\nsubOptions: "
            + str(self.subOptions)
            + "\nextraOptions: "
            + str(self.extraOptions)
        )


def runBM(runDesc, tests=None):
    """Run a specific benchmark with the required arguments"""

    if not tests:
        # Run all tests
        tests = list(runDesc.subOptions.keys())

    # print ("tests: ", tests)
    # print ("subOptions: ", subOptions)

    # print(runDesc)
    env = computeEnv()
    for test in tests:
        for opt in runDesc.subOptions.get(test, ("",)):
            for extra in runDesc.extraOptions.get(test, ("",)):
                execute(
                    env + runDesc.image + " " + test + opt + " " + extra,
                    outputName(runDesc.outputNamePrefix + test + opt + "_" + extra),
                )
