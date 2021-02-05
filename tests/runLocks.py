#===-- tests/runLocks.py - Run the different lock tests  -------*- C -*-===#
#
# Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
# See https:#llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===#
#
# \file
# This file contains simple tests for OpenMP static scheduling.
# It chooses a variety of different loops (increasing and decreasing) with
# strides, and then checks that all iterations are covered, and no iteration
# is executed more than once.
#
#===----------------------------------------------------------------------===#
import subprocess
import platform
import os

# Work around weird MacOS behaviour (maybe security related?)
# The DYLD_LIBRARY_PATH is *not* passed on to a grandchild process
# by default, so we have to pass it explicitly.
dyldPath = os.getenv("DYLD_LIBRARY_PATH")
env = "" if not dyldPath else "DYLD_LIBRARY_PATH="+dyldPath

def execute(cmd):
    print ("Running " + cmd)
    subprocess.run(cmd, shell=True)

arch = platform.machine()
def getExecutable(image):    
    return "./" + arch + "/" + image

image = getExecutable("test_locks")

# The list of locks to be tested    
locks = ["TTAS","MCS","cxx","pthread"]
if arch == "x86_64":
    locks += ("speculative",)

for lock in locks:
    execute("LOMP_LOCK_KIND=" + lock + " " + env + " " + image)
    
