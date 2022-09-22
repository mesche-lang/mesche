#include <stdlib.h>
#include <string.h>

#include "io.h"
#include "object.h"
#include "util.h"
#include "value.h"
#include "vm.h"

Value core_pair_p_msc(MescheMemory *mem, int arg_count, Value *args) {
  // TODO: Ensure single argument
  return BOOL_VAL(IS_CONS(args[0]));
}

Value core_symbol_p_msc(MescheMemory *mem, int arg_count, Value *args) {
  // TODO: Ensure single argument
  return BOOL_VAL(IS_SYMBOL(args[0]));
}

Value core_function_p_msc(MescheMemory *mem, int arg_count, Value *args) {
  // TODO: Ensure single argument
  return BOOL_VAL(IS_CLOSURE(args[0]) || IS_FUNCTION(args[0]) || IS_NATIVE_FUNC(args[0]));
}

Value core_array_p_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires 1 parameter.");
  }

  return BOOL_VAL(IS_ARRAY(args[0]));
}

Value core_car_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (!IS_CONS(args[0])) {
    PANIC("Object is not a pair: %d\n", AS_OBJECT(args[0])->kind);
  }

  ObjectCons *current_cons = AS_CONS(args[0]);
  return current_cons->car;
}

Value core_cdr_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (!IS_CONS(args[0])) {
    PANIC("Object is not a pair: %d\n", AS_OBJECT(args[0])->kind);
  }

  ObjectCons *current_cons = AS_CONS(args[0]);
  return current_cons->cdr;
}

Value core_append_msc(MescheMemory *mem, int arg_count, Value *args) {
  Value result = EMPTY_VAL;

  // Is there at least one argument?
  if (arg_count > 1) {
    // Walk all of the list arguments to append them together
    int arg_index = 0;
    Value result = EMPTY_VAL;
    Value prev_list_end = EMPTY_VAL;
    while (arg_index < arg_count) {
      Value current_list = args[arg_index];
      if (IS_EMPTY(current_list)) {
        // Move to the next argument
        arg_index++;
      } else if (IS_CONS(current_list)) {
        // The first legitimate list is the result
        if (IS_EMPTY(result)) {
          result = current_list;
        }

        // Should we attach the list to the previous list?
        if (IS_CONS(prev_list_end)) {
          AS_CONS(prev_list_end)->cdr = current_list;
          prev_list_end = EMPTY_VAL;
        }

        // Find the end of the list
        Value current_cons = current_list;
        while (IS_CONS(current_cons)) {
          if (IS_EMPTY(AS_CONS(current_cons)->cdr)) {
            // The current cons is the last position of the current list,
            // the next list or value will go in the cdr of this cell
            prev_list_end = current_cons;
            break;
          } else {
            // TODO: Error if not cons or empty!
          }

          current_cons = AS_CONS(current_cons)->cdr;
        }

        // Move to the next argument
        arg_index++;
      } else {
        // Argument is invalid if it's not the last argument and isn't a cons.
        // For now just skip it.
        arg_index++;
      }
    }

    return result;
  }

  // If no arguments are passed, return the empty list
  return EMPTY_VAL;
}

void mesche_core_module_init(VM *vm) {
  mesche_vm_define_native_funcs(
      vm, "mesche core",
      (MescheNativeFuncDetails[]){{"pair?", core_pair_p_msc, true},
                                  {"symbol?", core_symbol_p_msc, true},
                                  {"array?", core_array_p_msc, true},
                                  {"function?", core_function_p_msc, true},
                                  {"car", core_car_msc, true},
                                  {"cdr", core_cdr_msc, true},
                                  {"append", core_append_msc, true},
                                  {NULL, NULL, false}});
}
