#include <stdlib.h>
#include <string.h>

#include "io.h"
#include "object.h"
#include "util.h"
#include "value.h"
#include "vm.h"

Value pair_p_msc(MescheMemory *mem, int arg_count, Value *args) {
  // TODO: Ensure single argument
  return BOOL_VAL(IS_CONS(args[0]));
}

Value symbol_p_msc(MescheMemory *mem, int arg_count, Value *args) {
  // TODO: Ensure single argument
  return BOOL_VAL(IS_SYMBOL(args[0]));
}

Value function_p_msc(MescheMemory *mem, int arg_count, Value *args) {
  // TODO: Ensure single argument
  return BOOL_VAL(IS_CLOSURE(args[0]) || IS_FUNCTION(args[0]) || IS_NATIVE_FUNC(args[0]));
}

Value array_p_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires 1 parameter.");
  }

  return BOOL_VAL(IS_ARRAY(args[0]));
}

Value car_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (!IS_CONS(args[0])) {
    PANIC("Object is not a pair: %d\n", AS_OBJECT(args[0])->kind);
  }

  ObjectCons *current_cons = AS_CONS(args[0]);
  return current_cons->car;
}

Value cdr_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (!IS_CONS(args[0])) {
    PANIC("Object is not a pair: %d\n", AS_OBJECT(args[0])->kind);
  }

  ObjectCons *current_cons = AS_CONS(args[0]);
  return current_cons->cdr;
}

void mesche_core_module_init(VM *vm) {
  mesche_vm_define_native_funcs(vm, "mesche core",
                                (MescheNativeFuncDetails[]){{"pair?", pair_p_msc, true},
                                                            {"symbol?", symbol_p_msc, true},
                                                            {"array?", array_p_msc, true},
                                                            {"function?", function_p_msc, true},
                                                            {"car", car_msc, true},
                                                            {"cdr", cdr_msc, true},
                                                            {NULL, NULL, false}});
}
