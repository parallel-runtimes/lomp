# Little OpenMP\* Runtime

LOMP, short for Little OpenMP (runtime), is a small OpenMP runtime
implementation that can be used for educational or prototyping purposes.  It
currently only implements a rather small subset of the [OpenMP Application
Programming Interface](https://www.openmp.org/) for CPUs (i.e., it has no
support for offload to target devices and is also missing many
CPU-only features).

LOMP was written to demonstrate the design principals outlined in the book
[High-Performance Parallel Runtimes](https://www.degruyter.com/view/title/565255).

The library uses the same binary interface as clang/LLVM\*, and this is
compatible with several compilers that use that interface.  Unless you use a
feature that LOMP does not currently support, LOMP can serve as a drop-in
replacement for the native OpenMP runtime library of a compatible
compiler without requiring re-compilation of your application.

The runtime is mostly written in C++14, though with some features of C++17, and
can be compiled for a variety of different architectures.  There are **no**
assembler files in the runtime, and the use of inline assembly is restricted to
a few features (such as reading the high resolution clock).  For architectures
that do not have a code path to access the high-resolution clock via inline
assembly, we rely on C++ features to measure time.  Atomic operations are all
accessed though `std::atomic`.

As well as the source for the runtime there are also a few micro-benchmarks and
some (extremely minimal) sanity tests.

As its name suggest, the LOMP library is significantly smaller than the
production LLVM OpenMP runtime library.  At the time of this initial check-in,
the [cloc](https://github.com/AlDanial/cloc) utility in the source directory
shows under 6,000 lines of C++ code (and no assembly code).  For comparison,
the production [LLVM OpenMP runtime](https://github.com/llvm/llvm-project/tree/main/openmp)
has around 63,500 lines of C/C++ code and 1400 lines of assembly code in the
CPU part of the library.  Of course, this is an unfair comparison, since LOMP
is missing many features which are supported by the production runtime,
however it does make the point that if you want somewhere to
experiment, or an environment in which to set a student project, LOMP
may be an easier codebase on which to work!


## Supported Target Platforms

The LOMP runtime supports the following target architectures (in parenthesis we
show the architecture name as reported by the `uname` command):

* AMD\* Processors  (x86\_64)
* Arm\* Processors, 32 bit (armv7l)
* Arm Processors, 64 bit (aarch64)
* Intel\* Processors (x86\_64)
* RISC-V\* Processors, 64 bit (riscv64)

There is incomplete support for ARM-based Apple\* Macs (which is
held up by an LLVM compiler [bug](https://bugs.llvm.org/show_bug.cgi?id=48885)).


## Supported OpenMP Features

The language supported by LOMP is restricted to a small subset of the OpenMP API
for shared-memory multi-threading.  Supported OpenMP features are

* the `parallel` construct, including the data-sharing clauses `shared`,
  `private`, `firstprivate`, and `lastprivate`;
* the `master` and `single` constructs;
* the `barrier` construct;
* reductions (though not yet up a tree at the barrier);
* worksharing constructs `for` (C/C++) and `do` (Fortran);
* scheduling types `static`, `dynamic`, `auto` ,and `guided`, along with
  the additional `monotonic` and `nonmonotonic` qualifiers;
* the `critical` construct;
* the `flush` construct;
* the `task` construct, including the data-sharing clauses `shared`, `private`,
  and `firstprivate`;
* the `taskwait` construct; and
* the `taskgroup` construct.

The supported OpenMP runtime routines are:

* `omp_get_thread_num()` and `omp_get_num_threads()`;
* `omp_set_num_threads()` but only if performed before the runtime is
  initialized or if it is setting the same value as is already in use;
* `omp_get_max_threads()` and `omp_in_parallel()`;
* `omp_set_schedule()` and `omp_get_schedule()`;
* `omp_init_lock()`, `omp_set_lock()`, `omp_unset_lock()`, and
   `omp_destroy_lock()`; and
* `omp_get_wtime()`.


## Important Things Which Are Not Yet Supported

Since this is a small and relatively simple runtime (at least for now), there
are few restrictions and many things which have not yet been implemented.  A,
possibly incomplete, list is:

* nested parallelism;
* changing the number of threads;
* nested locks;
* more elaborate tasking features such as task dependences and `taskloop`;
* parsing many of the OpenMP-mandated environment variables (beyond
  `OMP_NUM_THREADS`), and support for their related internal control variables;
* explicitly controlling thread affinity;
* the [OMPT and OMPD](https://www.openmp.org/spec-html/5.1/openmpse5.html#x24-230001.5ce)
  profiling and debugging interfaces;
* ordered loops;
* the [`teams`](https://www.openmp.org/spec-html/5.1/openmpse15.html#x62-620002.7)
  and
  [`distribute`](https://www.openmp.org/spec-html/5.1/openmpsu50.html#x75-800002.11.6.1) constructs.
* the [cancellation constructs](https://www.openmp.org/spec-html/5.1/openmpse28.html#x144-1560002.20);
* offloading to accelerator devices, such as GPUs; and
* probably other things which we haven't noticed!

The runtime is also, of course, limited in the language it can support
by the compiler.  There are therefore some OpenMP 5.1 features which
are not yet implemented since there is no compiler support for them yet.

If you would like to contribute any features to LOMP, please see below.


## How to Build

Here are some, hopefully useful, remarks about how you can setup LOMP on your
system.  The instructions come without any warranty, and may be wrong, or
incomplete.

### Software Versions
To build the LOMP library, you need the following software environment:

* CMake, minimum version 3.13.0
* A clang-compatible compiler, one of:
    * clang, minimum version 10.0.0
    * AOCC, minimum version 2.2.0
    * Intel Next-gen Compiler (from Intel oneAPI), minimum version 2020.0
* libnuma, minimum version 2.0 (optional)
* Python, minimum version 3.x (optional; required for the
  micro-benchmarks but not the library itself)

Other versions of software tools may work, but we have not tested them with our
code.

While the LOMP library itself compiles fine with the GNU Compiler Collection
(GCC), we have not implemented all of the entry points that GCC requires for
its OpenMP support.  So, while you can compile the LOMP runtime code with GCC,
you will need a clang-compatible compiler to generate code to exercise the
LOMP library that you have built.

The micro-benchmarks (in the directory `microBM`) should work with any
OpenMP implementation.

### Building LOMP

Building LOMP follows the usual process of building a CMake-based project.
Here are the steps needed:

* Checkout LOMP from the project website:
  * via SSH: `git clone git@github.com:parallel-runtimes/lomp.git`
  * via HTTPS: `git clone https://github.com/parallel-runtimes/lomp.git`
* Create a build directory for an out-of-tree build: `mkdir lomp_build`
* Go to that new directory and run cmake there (before doing this, check the [CMake Configuration Options](#cmake-configuration-options) section below;
  you will probably need to add some options to set the appropriate compiler).
  `cd lomp_build`
  `cmake ../lomp`
* Compile LOMP (depending which build system you asked cmake to create
  build files for)
  * with GNU Make: `make`
  * with Ninja: `ninja`
* Run one of the compiled examples from the `examples` folder, e.g., Hello
  World:
    ```
    $ ./examples/hello_world
    Before parallel region
    =======================================
    Hello World: I am thread 6, and my secrets are 42.000000 and 21
    Hello World: I am thread 4, and my secrets are 42.000000 and 21
    Hello World: I am thread 1, and my secrets are 42.000000 and 21
    Hello World: I am thread 2, and my secrets are 42.000000 and 21
    Hello World: I am thread 0, and my secrets are 42.000000 and 21
    Hello World: I am thread 5, and my secrets are 42.000000 and 21
    Hello World: I am thread 3, and my secrets are 42.000000 and 21
    Hello World: I am thread 7, and my secrets are 42.000000 and 21
    =======================================
    After parallel region
    $
    ```

* You can also run the (tiny) test suite using the following commands (please
  except a few failed tests for v0.1):
  * with GNU Make: `make test`
  * with Ninja: `ninja test`

The default build configuration is "Release" mode, which enables compiler optimizations.  Please see below for how to change this default.

To use LOMP with an existing code, once you have built the library, you should
be able to use `LD_LIBRARY_PATH` on Linux (or `DYLD_LIBRARY_PATH` on MacOS) to
place its directory before the system one where the production OpenMP library
lives so that LOMP is used without needing to recompile your executable.  If
you also set `LOMP_DEBUG=1` you should see some output that proves that you are
using the library you expect. (Of course, the
[`ldd`](https://man7.org/linux/man-pages/man1/ldd.1.html) command on Linux can
also show you that.)

If you compiled the Hello World example that comes with LOMP using one of the
supported OpenMP compilers and if LOMP has been compiled in `$HOME/build_lomp`,
the following will dynamically bind LOMP to your compiled code:

```
$ export LD_LIBRARY_PATH=$HOME/build_lomp/src/:$LD_LIBRARY_PATH
$ LOMP_DEBUG=1 OMP_NUM_THREADS=4 ./a.out
Before parallel region
=======================================
LOMP:runtime version 0.1 (SO version 1) compiled at 19:26:59 on Jan 28 2021
from Git commit 0abcdef for x86_64 by LLVM:11:0:0
LOMP:with configuration -mrtm;-mcx16;DEBUG=10;LOMP_GNU_SUPPORT=1;LOMP_HAVE_RTM=1;LOMP_HAVE_CMPXCHG16B=1
Hello World: I am thread 1, and my secrets are 42.000000 and 21
Hello World: I am thread 2, and my secrets are 42.000000 and 21
Hello World: I am thread 0, and my secrets are 42.000000 and 21
Hello World: I am thread 3, and my secrets are 42.000000 and 21
=======================================
After parallel region
$
```

By using the `export` statement you will make LOMP your default OpenMP
runtime for processes started from this shell.  If you didn't want
that, remember to reset `LD_LIBRARY_PATH`.

### CMake Configuration Options

The following options can be set using the `cmake` command line interface:

* `-G Ninja`: Sets the build system to Ninja (the default is GNU Make)
* `-DCMAKE_C_COMPILER=xyz`: Set the C compiler to be `xyz`, the GNU Compiler
  Collection (GCC) is the default on most systems, but we want `clang`. (If you
  have `clang` in your path you can use `-DCMAKE_C_COMPILER=clang`).
* `-DCMAKE_CXX_COMPILER=xyz`: Set the C++ compiler to be `xyz`, GCC's `g++` is
  the default on most systems, but we want `clang++`. (If you have `clang++` in
  your path you can use `-DCMAKE_CXX_COMPILER=clang++`.)
* `-DLOMP_BUILD_EXAMPLES=[on|off]`: `on` builds the examples, `off` does not.
  The default is `on`.
* `-DLOMP_BUILD_MICROBM=[on|off]`: `on` builds the micro-benchmarks,
  `off` does not, the default is `on`.
* `-DLOMP_MICROBM_WITH_LOMP=[on|off]`: `on` links the micro-benchmarks with
  LOMP,  `off` links them  against the native OpenMP runtime of the compiler
  being used; the  default is `off`.
* `-DCMAKE_BUILD_TYPE=[release|debug|relwithdebinfo]`: `release` builds with
  optimization; `debug` builds without optimization and with debug
  information; `relwithdebinfo` builds with optimizations and debug
  information.
  The default is `release`.
* `-DCMAKE_VERBOSE_MAKEFILE=[on|off]`: `on` shows compiler invocation, while
  `off` does not.  The default is `off`.
* `-DLOMP_GNU_SUPPORT=[on|off]`: `on` builds GCC entry points for libgomp;
  `off ` does not build these entry points; the default is `off`.  This option
  is for the brave and will likely produce errors, as most these entry points
  have not yet  been implemented.
* `-DLOMP_ICC_SUPPORT=[on|off]`: `on` builds entry points for the Intel classic
  compiler, `off` does not build these entry points; the default is `off`. This
  option is for the brave and will likely produce errors, as most of these
  entry points have not been implemented.
* `-DLOMP_WARN_API_STUBS=[on|off]`: `on` emits warnings about entry point
  stubs; `off` does not; the default is `on`.
* `-DLOMP_WARN_ARCH_FEATURES=[on|off]`: `on` emits a warning if a dummy
  function is used for an unsupported architectural feature; `off` does not;
  the default is `on`.

### Environment Variables

The LOMP runtime library supports various environment variables that control
its behavior:

* `OMP_NUM_THREADS`: set the number of threads, see the
  [OpenMP specification](https://www.openmp.org/spec-html/5.1/openmpse59.html#x325-5000006.2)
  for details.
* `OMP_SCHEDULE`: set the loop schedule for loops that were compiled
  with the `runtime` schedule, see the
  [OpenMP specification](https://www.openmp.org/spec-html/5.1/openmpse58.html#x324-4990006.1)
  for details.
* `LOMP_LOCK_KIND`: This environment variable controls the lock implementation
  that LOMP uses for OpenMP locks.  The default is to use the C++ `std::mutex`
  lock.  The values it supports are:
  * `TTAS`: use test and test-and-set lock.
  * `MCS`: use a fair, scalable lock based on the ideas of Mellor-Crummey
     and Scott.
  * `cxx`: use the `std::mutex` lock of C++.  This is the default.
  * `pthread`: use the `pthread_mutex` (this requires that the C++
    `std::thread` implementation is based on pthreads, which may not be
    true on all platforms).
  * `speculative`: use a speculative lock that internally uses TTAS as
    the fallback lock (this needs support for speculative execution in
    hardware).
* `LOMP_BARRIER_KIND`: Controls which barrier implementation from LOMP's
  barrier zoo is used.  There are too many to list here, so "Use the source, Luke!"
* `LOMP_DEBUG`: Enable printing of debugging messages.  The higher the level,
  the more debugging output will appear on the screen, higher numbers will also
  enable the lower-number levels.  Useful levels are:
  * `0`: show no debug messages.  This is the default.
  * `1`: print the library's name, target, and compilation information.
  * `2`: print informational messages.
  * `10`: print  more details.
  * `20`: print debugging messages for LOMP threading subsystems.
  * `30`: print debugging messages for barriers.
  * `40`: print debugging messages for loop scheduling.
  * `50`: print debugging messages for lock implementations.
  * `1000`: print debugging messages for internal function invocations.
* `LOMP_TRACE`: Enable LOMP's internal tracing facility, setting the debug
  level to the value specified for `LOMP_TRACE`.  See `LOMP_DEBUG` for the
  supported levels.

Note that when debugging the library it is often convenient to change the order
of the debug tags, so as only to print information from the subsystem of
interest, so the values for `LOMP_DEBUG` may change over time.


### Micro-Benchmarks

The micro-benchmarks are in the `microBM` directory.  These were used to
measure hardware properties shown in the book.  You can use them to measure the
properties of your own machines.  Each benchmark can be invoked by an
appropriate Python script which will run all of the available measurements, or
the ones which you request and write appropriately named files containing the
results.

To use the scripts, ensure that your current directory is the `microBM`
directory in the appropriate build, then execute the relevant
Python script from the microBM source directory, e.g.
```
    $ cd lomp_build/microBM
    $ python ~/lomp/microBM/runAtomics.py
    Arch (may be wrong!):
    Model:
    Cores:  8
    Running  OMP_NUM_THREADS=8 KMP_HW_SUBSET=1T KMP_AFFINITY='compact,granularity=fine' ./atomics Ie > AtomicsIe_Mac-mini_2021-01-28_1.res
    ./atomics Ie
    ........
    Running  OMP_NUM_THREADS=8 KMP_HW_SUBSET=1T KMP_AFFINITY='compact,granularity=fine' ./atomics If > AtomicsIf_Mac-mini_2021-01-28_1.res
    ./atomics If
    ........
    Running  OMP_NUM_THREADS=8 KMP_HW_SUBSET=1T KMP_AFFINITY='compact,granularity=fine' ./atomics Ii > AtomicsIi_Mac-mini_2021-01-28_1.res
    ./atomics Ii
    ........
    Running  OMP_NUM_THREADS=8 KMP_HW_SUBSET=1T KMP_AFFINITY='compact,granularity=fine' ./atomics It > AtomicsIt_Mac-mini_2021-01-28_1.res
    ./atomics It
    ........
    $
```

The output files can be converted into web pages containing tabulated results
and plots by using the `plot.py` script in the `scripts` directory and feeding
it the various files for a specific measurement, and should also be easy to
read.  If you feel the desperate need to use a spreadsheet, the `toCSV.py`
script in the `scripts` directory will convert them into a comma-separated
file format which can be read into whichever spreadsheet you suffer.


## Bugs

Please submit bug reports or other feedback via GitHub issues by filling in the
issue templates that are provided.


## Contributions

Contributions are welcome, as is other feedback.  We hope that this
runtime will provide a useful environment for experimentation with new
OpenMP features (such as different loop schedules, or barrier
implementations), while remaining simple enough to be easy to use in a
university course.

If you want to contribute (for fame and glory), please fork the repository and
submit a pull request with the changes you would like to make


## License

The LOMP runtime is licensed under the "[Apache 2.0 License with LLVM
exceptions](https://llvm.org/docs/DeveloperPolicy.html#new-llvm-project-license-framework)"
license.  Any contributions must use that license to be acceptable.


## Reference

If you use LOMP in your research and publish a paper, please use the following
in your citation:

Jim Cownie and Michael Klemm, *Little OpenMP Runtime*,
https://github.com/parallel-runtimes/lomp, February 2021.


## Contributors

* Jim Cownie <jcownie@acm.org>
* Michael Klemm <michael@dontknow.de>

## Trademarks
Trademarks and registered names are marked with an asterisk (\*) at their first
use where we have recognized them.  Other names and trademarks may be the
property of others.
