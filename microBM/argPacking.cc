//===------------------------------------------------------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

/*
 * Code to demonstrate packing and unpacking arguments, and that we can do that
 * in a way which does not even require assembler code. (Though it is outside 
 * the language standards, so is not guaranteed to work...)
 *
 * The problem we have is that the compiler interface used by LLVM for outlined
 * OpenMP parallel regions passes each argument to the outlined region
 * separately, rather than packing them into a vector. 
 * Therefore, if we have OpenMP code like this
 *
 * int sumInParallel(int n, int * v)
 *  {
 *     int sum = 0;
 *  #pragma omp parallel for reduction(+:sum)
 *     for (int i=0; i<n; i++)
 *         sum += v[i];
 *     return sum;
 *  }
 *
 *
 * The code which is generated to call __kmpc_fork_call passes 3 void *
 * arguments (one for each of n, v, and sum, all of which are shared
 * variables accessed from inside the parallel region) as well as a
 * pointer to a descriptor for the source code location, the outlined
 * function and an argument count.
 *
 * The outlined function then has a prototype which looks like
 * body(void *, void *, void *);
 * I.e. it expects the three arguments to be passed by the normal calling
 * convention in specific registers.
 *
 * By looking at the assembler code generated for code like that below, (for the cases
 * up to 16 arguments!)
 *
 * extern void f0();
 * extern void f1(void * arg1);
 * extern void f2(void * arg1, void * arg2);
 * extern void f3(void * arg1, void * arg2, void * arg3);
 * extern void f4(void * arg1, void * arg2, void * arg3, void * arg4);
 * extern void f5(void * arg1, void * arg2, void * arg3, void * arg4,
 *                void * arg5);
 * extern void f6(void * arg1, void * arg2, void * arg3, void * arg4,
 *                void * arg5, void * arg6);
 * extern void f7(void * arg1, void * arg2, void * arg3, void * arg4,
 *                void * arg5, void * arg6, void * arg7);
 * extern void f8(void * arg1, void * arg2, void * arg3, void * arg4,
 *                void * arg5, void * arg6, void * arg7, void * arg8);
 * ... etc ...
 * void test()
 * {
 *     int args[16];
 *
 *     f0();
 *     f1(&args[0]);
 *     f2(&args[0], &args[1]);
 *     f3(&args[0], &args[1], &args[2]);
 *     f4(&args[0], &args[1], &args[2], &args[3]);
 *     f5(&args[0], &args[1], &args[2], &args[3],
 *        &args[4]);
 *     f6(&args[0], &args[1], &args[2], &args[3],
 *        &args[4], &args[5]);
 *     f7(&args[0], &args[1], &args[2], &args[3],
 *        &args[4], &args[5], &args[6]);
 *     f8(&args[0], &args[1], &args[2], &args[3],
 *        &args[4], &args[5], &args[6], &args[7]);
 *     ... etc ...
 * }
 *
 * we can see how arguments are passed.
 *
 * On X86_64, we see that the first six
 * arguments are passed in rdi, rsi, rdx, rcx, r8 and r9, additional
 * arguments are pushed onto the callers stack.
 * On AARCH64, we can see that first eight arguments are passed in registers
 * X0:X7, and additional arguments are pushed onto the callers stack.
 *
 * In both cases this means that we cannot avoid copying arguments beyond
 * those in the registers, since they have to be in the callers stack as they
 * are implicitly accessed via the incoming stackpointer, which is different
 * in each thread.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#define STRINGIFY1(X) #X
#define STRINGIFY(...) STRINGIFY1(__VA_ARGS__)

typedef void (*BodyType)(...);

class InvocationInfo {
  BodyType Body;
  int ArgCount;
  va_list * Args;

public:
  InvocationInfo(BodyType B, int AC, va_list * A)
      : Body(B), ArgCount(AC), Args(A) {}
  void run() const;
};

/* Should go into a header... */
#define AARCH64 1
#define X86_64 2

static void dumpMemory(int N, void * Memory) {
  uintptr_t * P = (uintptr_t *)Memory;

  for (int i = 0; i < N; i++) {
    if (i % 4 == 0)
      printf("\n0x%016lx: ", uintptr_t(&P[i]));
    printf("0x%016lx, ", P[i]);
  }
  printf("\n");
}

/* va_list is unpleasant, since it contains internal state about the point at which fetching has occurred,
 * which means that a single va_list cannot be used by multiple threads. Since we want to be able to do that 
 * we have to be more subtle, and provide our own accessor into the va_list which does not maintain internal 
 * state there.
 */

#if (TARGET == AARCH64)
class va_listAccessor {
  // Definition from https://developer.arm.com/docs/ihi0055/latest/procedure-call-standard-for-the-arm-64-bit-architecture
  // Search for "va_list type"
  // Our case is simplified since we know that all of our arguments are of type void *.
  // That means that there are no FP arguments to worry about, so we only have two main cases to consider.
  struct VAListImpl {
    void * stack;  // next stack param
    void * gr_top; // end of GP arg reg save area
    void * vr_top; // end of FP/SIMD arg reg save area
    int gr_offs;   // offset from  gr_top to next GP register arg
    int vr_offs;   // offset from  vr_top to next FP/SIMD register arg
    void dump() const {
      printf("va_list:\n"
             "  stack:  0x%016lx -> 0x%016lx\n"
             "  gr_top: 0x%016lx\n"
             "  vr_top: 0x%016lx\n"
             "  gr_offs: %d\n"
             "  vr_offs: %d\n",
             uintptr_t(stack), *(uintptr_t *)stack, uintptr_t(gr_top),
             uintptr_t(vr_top), gr_offs, vr_offs);
    }
  };

  VAListImpl const * Target;
  // If no registers are saved in the register save area (which can happen if the stack save area and register save area are
  // contiguous, under which circumstances they are all treated as stack-saved), then gr_offs and gr_top will both be zero.
  enum { MaxSavedRegisters = 8 };
  int getRegisterSaveCount() const {
    return (-Target->gr_offs) / sizeof(void *);
  }
  void ** getGRBase() const {
    return ((void **)Target->gr_top) - getRegisterSaveCount();
  }

public:
  va_listAccessor(va_list * Alist) : Target((VAListImpl *)Alist) {
    Target->dump();
  }
  void ** getSaveAreaBase() const {
    return (void **)Target->stack;
  }

  /* Functions which are architecture independent. */
  void * getArg(int ArgNo) const;
  int getStackArgumentCount(int ArgCount) const;
  int getMaxSavedRegisters() const;
  void dump(int NArgs) const;
};

#elif (TARGET == X86_64)
class va_listAccessor {
  struct VAListImpl {
    unsigned int gp_offset;      // Offset (in bytes) into reg_save_area
    unsigned int fp_offset;      //
    uintptr_t overflow_arg_area; // Pointer to overflow (non-register) arguments
    uintptr_t reg_save_area;

#if (0)
    void dump() const {
      printf("va_list:\n"
             "  gp_offset: %u\n"
             "  fp_offset: %u\n"
             "  overflow_arg_area: %p\n"
             "  reg_save_area: %p\n",
             gp_offset, fp_offset, (void *)overflow_arg_area,
             (void *)reg_save_area);
      dumpMemory(16, (void **)overflow_arg_area);
      dumpMemory(16, (void **)reg_save_area);
    }
#else
    void dump() const {}
#endif
  };

  VAListImpl const * Target;

  enum { MaxSavedRegisters = 6 };
  // In the general case this will depend on the number of initial arguments which are not passed via the ellipsis.
  int getRegisterSaveCount() const {
    int RegCount = MaxSavedRegisters - (Target->gp_offset / sizeof(void *));
    return RegCount > 0 ? RegCount : 0;
  }
  void ** getGRBase() const {
    return (void **)(Target->reg_save_area +
                     sizeof(void *) * (6 - getRegisterSaveCount()));
  }

public:
  va_listAccessor(va_list * Alist) : Target((VAListImpl *)Alist) {
    Target->dump();
    // printf ("Register save count: %d\n", getRegisterSaveCount());
  }
  void ** getSaveAreaBase() const {
    return (void **)(Target->overflow_arg_area);
  }

  void * getArg(int ArgNo) const;
  int getStackArgumentCount(int ArgCount) const;
  void dump(int NArgs) const;
  int getMaxSavedRegisters() const;
};
#else
#error Unknown TARGET architecture
#endif

// Common code, that doesn't depend on the architecture.
// (At least, it can handle AARCH64 and X86_64 :-)).

int va_listAccessor::getMaxSavedRegisters() const {
  return MaxSavedRegisters;
}

int va_listAccessor::getStackArgumentCount(int ArgCount) const {
  return ArgCount > MaxSavedRegisters ? ArgCount - MaxSavedRegisters : 0;
}

void va_listAccessor::dump(int NArgs) const {
  for (int i = 0; i < NArgs; i++) {
    uintptr_t Arg = uintptr_t(getArg(i));
    printf("  %2d: %016lx\n", i, Arg);
  }
  printf(")\n");
}

void * va_listAccessor::getArg(int ArgNo) const {
  int StackIdx = ArgNo - getRegisterSaveCount();

  // printf ("ArgNo: %d, StackIdx: %d\n", ArgNo, StackIdx);

  // Check whether this argument is in the register save area, or on the stack and fetch it
  // from the appropriate place.
  if (StackIdx < 0)
    return getGRBase()[ArgNo];
  else
    return getSaveAreaBase()[StackIdx];
}

/* Invoke a function passing it the arguments it requires.
 * For complete safety this would require assembler code since 
 * in the case where we need to push arguments into the stack, we must
 * allocate space on the stack and ensure that it is at the current
 * bottom of the stack. There is no way guaranteed by the C/C++ standard to
 * do that.
 * However... it seems that implementations of stack variable length arrays [VLA]
 * do the "obvious" thing, and allocate space for the VLA by pulling the
 * stack down. Therefore the most recently allocated VLA is in the right place!
 * So code like this works.
 */
void InvocationInfo::run() const {
  va_listAccessor OurAccessor(Args);
#if (1)
  printf("run: ArgCount %d: (\n", ArgCount);
  OurAccessor.dump(ArgCount);
#endif
  // Tuning options to consider here :-
  //
  // 1) Flatten the arguments into a local array and pull them from there, rather than having each case in the switch statement
  //    use the accessor. However, I am *hoping* that the compiler is smart enough not to need to store the values
  //    after it has loaded them, since their destination is a register. And the accessor code should be only a few instructions.
  //
  // 2) Don't bother with the switch, but rather just always pass all of the register arguments. That is invalid, since it
  //    will access uninitialized values from the callers stack, but it won't cause a crash, and teh extra loads may be
  //    cheaper than badly predicted branches if there are many interleaved parallel regions with different numbers of arguments.
  //
  switch (ArgCount) {
  case 0:
    Body();
    return;
  case 1:
    Body(OurAccessor.getArg(0));
    return;
  case 2:
    Body(OurAccessor.getArg(0), OurAccessor.getArg(1));
    return;
  case 3:
    Body(OurAccessor.getArg(0), OurAccessor.getArg(1), OurAccessor.getArg(2));
    return;
  case 4:
    Body(OurAccessor.getArg(0), OurAccessor.getArg(1), OurAccessor.getArg(2),
         OurAccessor.getArg(3));
    return;
  case 5:
    Body(OurAccessor.getArg(0), OurAccessor.getArg(1), OurAccessor.getArg(2),
         OurAccessor.getArg(3), OurAccessor.getArg(4));
    return;
  case 6:
    Body(OurAccessor.getArg(0), OurAccessor.getArg(1), OurAccessor.getArg(2),
         OurAccessor.getArg(3), OurAccessor.getArg(4), OurAccessor.getArg(5));
    return;
#if (TARGET == AARCH64)
  case 7:
    Body(OurAccessor.getArg(0), OurAccessor.getArg(1), OurAccessor.getArg(2),
         OurAccessor.getArg(3), OurAccessor.getArg(4), OurAccessor.getArg(5),
         OurAccessor.getArg(6));
    return;
  case 8:
    Body(OurAccessor.getArg(0), OurAccessor.getArg(1), OurAccessor.getArg(2),
         OurAccessor.getArg(3), OurAccessor.getArg(4), OurAccessor.getArg(5),
         OurAccessor.getArg(6), OurAccessor.getArg(7));
    return;
#endif
  default: { // Need arguments on the stack
    int StackArgCount = OurAccessor.getStackArgumentCount(ArgCount);
    void * StackArgs[StackArgCount];

    // Copy the stack arguments
    // With a little more trickery we could likely use memcpy, but this may be
    // as good anyway, since we know alignment and so on.
    for (int i = 0; i < StackArgCount; i++)
      StackArgs[i] = OurAccessor.getArg(OurAccessor.getMaxSavedRegisters() + i);

    // Since the compiler can't see that the stack arguments matter we
    // have to ensure that it doesn't eliminate them completely!
    // Note that although this is assembly code, it is entirely portable
    // across architectures since it doesn't generate any instructions,
    // just gives information to the compiler.
    __asm__ volatile("# Ensure compiler doesn't eliminate our VLA since the "
                     "compiler doesn't know it's used by the called code"
                     :
                     : "r"(StackArgs));
    // Another alternative here would be
    // (void) * (void * volatile *)&StackArgs[0];
    // Though that will force the generation of a load operation, whereas
    // the asm code doesn't, it just tells the compiler that the array is
    // being accessed.

    // Finally apply the function. Note that the stack arguments are not
    // visible, since they are accessed implicilty via the incoming stack
    // pointer, which is why we needed the trickery avove to ensure that
    // the compiler didn't eliminate the StackArgs array and all the code
    // to initialize it!
#if (TARGET == X86_64)
    // Six arguments are passed in registers
    Body(OurAccessor.getArg(0), OurAccessor.getArg(1), OurAccessor.getArg(2),
         OurAccessor.getArg(3), OurAccessor.getArg(4), OurAccessor.getArg(5));
#elif (TARGET == AARCH64)
    // Eight arguments are passed in registers
    Body(OurAccessor.getArg(0), OurAccessor.getArg(1), OurAccessor.getArg(2),
         OurAccessor.getArg(3), OurAccessor.getArg(4), OurAccessor.getArg(5),
         OurAccessor.getArg(6), OurAccessor.getArg(7));
#else
#error Unknown TARGET
#endif

    return;
  }
  }
}

void packAndInvoke(int ArgCount, BodyType OutlinedBody, ...) {
  va_list PackedArgs;
  va_start(PackedArgs, OutlinedBody);

  InvocationInfo PackedBody(OutlinedBody, ArgCount, &PackedArgs);
  // Pass the packed body out to all of the worker threads.
  // and invoke it here.
  //
  // Their access to the stack here is OK, because there is a join barrier inside the
  // body function, so our invocation of "run" cannot return until they have all finished
  // which means that they must also have finished reading the PackedArgs from our stack.
  PackedBody.run();
  va_end(PackedArgs);
}

#if (1)
void test(int NumArgs, ...) {
  va_list VAL;

  va_start(VAL, NumArgs);
  va_listAccessor OurAccessor(&VAL);

  printf("test(%d\n", NumArgs);
  OurAccessor.dump(NumArgs);

  va_end(VAL);
}

/* This version just uses va_list, avoiding the use of our code the second time around
 * (This isn't really necessary, but simplifies debugging because there's only
 * one instance of our code to think about!)
 */
void testSimple(int NumArgs, ...) {
  va_list VAL;
  printf("Invoked function with %d arguments sees\n", NumArgs);
  va_start(VAL, NumArgs);
  for (int i = 0; i < NumArgs; i++)
    printf("  %2d: %016lx\n", i, va_arg(VAL, uintptr_t));
  va_end(VAL);
}

int main(int argc, char ** argv) {
  // Test cases with one to 15 args
  printf("TARGET: %s\n", STRINGIFY(TARGET));
#if (1)
  printf("Simple accessor tests\n");
#if (0)
  test(0);
  test(1, (void *)1);
  test(2, (void *)1, (void *)2);
  test(3, (void *)1, (void *)2, (void *)3);
  test(4, (void *)1, (void *)2, (void *)3, (void *)4);
#endif
  test(5, (void *)1, (void *)2, (void *)3, (void *)4, (void *)5);
  test(6, (void *)1, (void *)2, (void *)3, (void *)4, (void *)5, (void *)6);
  test(7, (void *)1, (void *)2, (void *)3, (void *)4, (void *)5, (void *)6,
       (void *)7);
  test(8, (void *)1, (void *)2, (void *)3, (void *)4, (void *)5, (void *)6,
       (void *)7, (void *)8);
  test(9, (void *)1, (void *)2, (void *)3, (void *)4, (void *)5, (void *)6,
       (void *)7, (void *)8, (void *)9);
  test(10, (void *)1, (void *)2, (void *)3, (void *)4, (void *)5, (void *)6,
       (void *)7, (void *)8, (void *)9, (void *)10);
  test(11, (void *)1, (void *)2, (void *)3, (void *)4, (void *)5, (void *)6,
       (void *)7, (void *)8, (void *)9, (void *)10, (void *)11);
  test(12, (void *)1, (void *)2, (void *)3, (void *)4, (void *)5, (void *)6,
       (void *)7, (void *)8, (void *)9, (void *)10, (void *)11, (void *)12);
#if (0)
  test(13, (void *)1, (void *)2, (void *)3, (void *)4, (void *)5, (void *)6,
       (void *)7, (void *)8, (void *)9, (void *)10, (void *)11, (void *)12,
       (void *)13);
  test(14, (void *)1, (void *)2, (void *)3, (void *)4, (void *)5, (void *)6,
       (void *)7, (void *)8, (void *)9, (void *)10, (void *)11, (void *)12,
       (void *)13, (void *)14);
  test(15, (void *)1, (void *)2, (void *)3, (void *)4, (void *)5, (void *)6,
       (void *)7, (void *)8, (void *)9, (void *)10, (void *)11, (void *)12,
       (void *)13, (void *)14, (void *)15);
#endif
#endif
#if (1)
  printf("Argument unpacking tests\n");
  // test always gets one argument, (the argument count itself)

  packAndInvoke(1, (BodyType)testSimple, (void *)0);
  packAndInvoke(2, (BodyType)testSimple, (void *)1, (void *)1);
  packAndInvoke(3, (BodyType)testSimple, (void *)2, (void *)1, (void *)2);
  packAndInvoke(4, (BodyType)testSimple, (void *)3, (void *)1, (void *)2,
                (void *)3);
  packAndInvoke(5, (BodyType)testSimple, (void *)4, (void *)1, (void *)2,
                (void *)3, (void *)4);
  packAndInvoke(6, (BodyType)testSimple, (void *)5, (void *)1, (void *)2,
                (void *)3, (void *)4, (void *)5);
  packAndInvoke(7, (BodyType)testSimple, (void *)6, (void *)1, (void *)2,
                (void *)3, (void *)4, (void *)5, (void *)6);
  packAndInvoke(8, (BodyType)testSimple, (void *)7, (void *)1, (void *)2,
                (void *)3, (void *)4, (void *)5, (void *)6, (void *)7);
  packAndInvoke(9, (BodyType)testSimple, (void *)8, (void *)1, (void *)2,
                (void *)3, (void *)4, (void *)5, (void *)6, (void *)7,
                (void *)8);
  packAndInvoke(10, (BodyType)testSimple, (void *)9, (void *)1, (void *)2,
                (void *)3, (void *)4, (void *)5, (void *)6, (void *)7,
                (void *)8, (void *)9);
#if (0)
  packAndInvoke(11, (BodyType)testSimple, (void *)10, (void *)1, (void *)2,
                (void *)3, (void *)4, (void *)5, (void *)6, (void *)7,
                (void *)8, (void *)9, (void *)10);
  packAndInvoke(12, (BodyType)testSimple, (void *)11, (void *)1, (void *)2,
                (void *)3, (void *)4, (void *)5, (void *)6, (void *)7,
                (void *)8, (void *)9, (void *)10, (void *)11);
  packAndInvoke(13, (BodyType)testSimple, (void *)12, (void *)1, (void *)2,
                (void *)3, (void *)4, (void *)5, (void *)6, (void *)7,
                (void *)8, (void *)9, (void *)10, (void *)11, (void *)12);
  packAndInvoke(14, (BodyType)testSimple, (void *)13, (void *)1, (void *)2,
                (void *)3, (void *)4, (void *)5, (void *)6, (void *)7,
                (void *)8, (void *)9, (void *)10, (void *)11, (void *)12,
                (void *)13);
  packAndInvoke(15, (BodyType)testSimple, (void *)14, (void *)1, (void *)2,
                (void *)3, (void *)4, (void *)5, (void *)6, (void *)7,
                (void *)8, (void *)9, (void *)10, (void *)11, (void *)12,
                (void *)13, (void *)14);
  packAndInvoke(16, (BodyType)testSimple, (void *)15, (void *)1, (void *)2,
                (void *)3, (void *)4, (void *)5, (void *)6, (void *)7,
                (void *)8, (void *)9, (void *)10, (void *)11, (void *)12,
                (void *)13, (void *)14, (void *)15);
  364
#endif
#endif
      return 0;
}
#endif
