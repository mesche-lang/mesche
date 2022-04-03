
#include "math.h"
#include "object.h"
#include "util.h"
#include <math.h>
#include <stdlib.h>
#include <time.h>

Value math_floor_msc(MescheMemory *mem, int arg_count, Value *args) {
  // TODO: Add type check
  return NUMBER_VAL(floor(AS_NUMBER(args[0])));
}

Value math_rand_int_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires 1 parameter.");
  }

  // TODO: Add type check
  return NUMBER_VAL(rand() % (int)AS_NUMBER(args[0]));
}

Value math_sin_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires 1 parameter.");
  }

  // TODO: Add type checks
  return NUMBER_VAL(sin(AS_NUMBER(args[0])));
}

Value math_abs_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires 1 parameter.");
  }

  // TODO: Add type checks
  return NUMBER_VAL(fabs(AS_NUMBER(args[0])));
}

Value math_min_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (arg_count != 2) {
    PANIC("Function requires 2 parameters.");
  }

  // TODO: Add type checks
  return NUMBER_VAL(fmin(AS_NUMBER(args[0]), AS_NUMBER(args[1])));
}

Value math_max_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (arg_count != 2) {
    PANIC("Function requires 2 parameters.");
  }

  // TODO: Add type checks
  return NUMBER_VAL(fmax(AS_NUMBER(args[0]), AS_NUMBER(args[1])));
}

void mesche_math_module_init(VM *vm) {
  // TODO: Add an API for setting the seed
  srand(time(NULL));

  mesche_vm_define_native_funcs(vm, "mesche math",
                                (MescheNativeFuncDetails[]){{"floor", math_floor_msc, true},
                                                            {"sin", math_sin_msc, true},
                                                            {"abs", math_abs_msc, true},
                                                            {"min", math_min_msc, true},
                                                            {"max", math_max_msc, true},
                                                            {"rand-int", math_rand_int_msc, true},
                                                            {NULL, NULL, false}});
}
