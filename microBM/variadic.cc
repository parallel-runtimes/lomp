#include <stdio.h>
#include <stdarg.h>
#include "stats-timing.h"

#define NOINLINE __attribute__((noinline))

void NOINLINE foo(int * args, int a1, int a2, int a3) {
  args[0] = a1;
  args[1] = a2;
  args[2] = a3;
}

void NOINLINE foo_ellipsis(int * args, ...) {
  va_list argList;
  va_start(argList, args);
  args[0] = va_arg(argList, int);
  args[1] = va_arg(argList, int);
  args[2] = va_arg(argList, int);
}

#define INNER_LOOPS 100
#define OUTER_LOOPS 1000

int main(int, char **) {
  int args[3];
  lomp::statistic fixedFn;
  lomp::statistic variadicFn;
  lomp::statistic ratio;

  for (int i = 0; i < OUTER_LOOPS; ++i) {
    lomp::tsc_tick_count startFixed;
    for (int i = 0; i < INNER_LOOPS; ++i) {
      foo(&args[0], 1, 2, 3);
    }
    lomp::tsc_tick_count startVariadic;
    for (int i = 0; i < INNER_LOOPS; ++i) {
      foo_ellipsis(&args[0], 1, 2, 3);
    }
    lomp::tsc_tick_count end;
    auto fixedTime = startVariadic - startFixed;
    auto variadicTime = end - startVariadic;
    fixedFn.addSample(fixedTime.seconds());
    variadicFn.addSample(variadicTime.seconds());
    ratio.addSample(variadicTime.seconds() / fixedTime.seconds());
  }

  // Scale the results down
  fixedFn.scaleDown(INNER_LOOPS);
  variadicFn.scaleDown(INNER_LOOPS);

  // Print the results
  printf("Function call times\n"
         "%s, %s, %s\n",
         Target::CPUModelName().c_str(),
#if (LOMP_TARGET_MACOS)
         "MacOS"
#elif (LOMP_TARGET_LINUX)
         "Linux"
#else
         "Unknown OS"
#endif
         ,
         COMPILER_NAME);
  printf("# %s\n"
         "Test,     Samples,    Min,   Mean,  Max, SD\n",
         lomp::tsc_tick_count::timerDescription().c_str());
  printf("Fixed Function, %s\n", fixedFn.format('s').c_str());
  printf("Variadic Function, %s\n", variadicFn.format('s').c_str());
  printf("Variadic/fixed, %s\n", ratio.format(' ').c_str());

  return 0;
}
