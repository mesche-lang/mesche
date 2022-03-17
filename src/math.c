
#include "math.h"
#include "object.h"
#include <math.h>

Value math_floor_msc(MescheMemory *mem, int arg_count, Value *args) {
  // TODO: Add type check
  return NUMBER_VAL(floor(AS_NUMBER(args[0])));
}

void mesche_math_module_init(VM *vm) {
  mesche_vm_define_native_funcs(
      vm, "mesche math",
      &(MescheNativeFuncDetails[]){{"floor", math_floor_msc, true}, {NULL, NULL, false}});
}
