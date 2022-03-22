
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

void mesche_math_module_init(VM *vm) {
  // TODO: Add an API for setting the seed
  srand(time(NULL));

  mesche_vm_define_native_funcs(vm, "mesche math",
                                &(MescheNativeFuncDetails[]){{"floor", math_floor_msc, true},
                                                             {"rand-int", math_rand_int_msc, true},
                                                             {NULL, NULL, false}});
}
