#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "closure.h"
#include "io.h"
#include "keyword.h"
#include "native.h"
#include "object.h"
#include "symbol.h"
#include "util.h"
#include "value.h"
#include "vm-impl.h"

Value core_number_p_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires a single parameter.");
  }

  return BOOL_VAL(IS_NUMBER(args[0]));
}

Value core_boolean_p_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires a single parameter.");
  }

  return BOOL_VAL(IS_TRUE(args[0]) || IS_FALSE(args[0]));
}

Value core_pair_p_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires a single parameter.");
  }

  return BOOL_VAL(IS_CONS(args[0]));
}

Value core_string_p_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires a single parameter.");
  }

  return BOOL_VAL(IS_STRING(args[0]));
}

Value core_symbol_p_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires a single parameter.");
  }

  return BOOL_VAL(IS_SYMBOL(args[0]));
}

Value core_keyword_p_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires a single parameter.");
  }

  return BOOL_VAL(IS_KEYWORD(args[0]));
}

Value core_function_p_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires a single parameter.");
  }

  return BOOL_VAL(IS_CLOSURE(args[0]) || IS_FUNCTION(args[0]) || IS_NATIVE_FUNC(args[0]));
}

Value core_array_p_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires a single parameter.");
  }

  return BOOL_VAL(IS_ARRAY(args[0]));
}

Value core_equal_p_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 2) {
    PANIC("Function requires 2 parameters.");
  }

  return BOOL_VAL(mesche_value_eqv_p(args[0], args[1]));
}

Value core_eqv_p_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 2) {
    PANIC("Function requires 2 parameters.");
  }

  return BOOL_VAL(mesche_value_eqv_p(args[0], args[1]));
}

Value core_not_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires 1 parameter.");
  }

  // Return true if the value is explicitly #f
  return BOOL_VAL(IS_FALSE(args[0]));
}

Value core_cons_msc(VM *vm, int arg_count, Value *args) {
  // TODO: Ensure two arguments
  return OBJECT_VAL(mesche_object_make_cons(vm, args[0], args[1]));
}

Value core_list_msc(VM *vm, int arg_count, Value *args) {
  Value list = EMPTY_VAL;

  if (arg_count > 0) {
    ObjectCons *current = NULL;
    bool pushed_list = false;
    for (int i = 0; i < arg_count; i++) {
      if (current == NULL) {
        current = mesche_object_make_cons(vm, args[i], EMPTY_VAL);
        list = OBJECT_VAL(current);

        // Temporarily store the list so it doesn't get collected
        mesche_vm_stack_push(vm, list);
      } else {
        // Add the new cons to the end of the list
        current->cdr = OBJECT_VAL(mesche_object_make_cons(vm, args[i], EMPTY_VAL));
        current = AS_CONS(current->cdr);
      }
    }

    // Pop the list from the stack before we return it
    mesche_vm_stack_pop(vm);
  }

  return list;
}

Value core_car_msc(VM *vm, int arg_count, Value *args) {
  if (!IS_CONS(args[0])) {
    PANIC("Object is not a pair: %d\n", AS_OBJECT(args[0])->kind);
  }

  ObjectCons *current_cons = AS_CONS(args[0]);
  return current_cons->car;
}

Value core_cdr_msc(VM *vm, int arg_count, Value *args) {
  if (!IS_CONS(args[0])) {
    PANIC("Object is not a pair: %d\n", AS_OBJECT(args[0])->kind);
  }

  ObjectCons *current_cons = AS_CONS(args[0]);
  return current_cons->cdr;
}

Value core_cadr_msc(VM *vm, int arg_count, Value *args) {
  if (!IS_CONS(args[0])) {
    PANIC("Object is not a pair: %d\n", AS_OBJECT(args[0])->kind);
  }

  ObjectCons *current_cons = AS_CONS(args[0]);
  return AS_CONS(current_cons->cdr)->car;
}

Value core_append_msc(VM *vm, int arg_count, Value *args) {
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

Value core_plus_msc(VM *vm, int arg_count, Value *args) {
  double result = 0.f;
  for (int i = 0; i < arg_count; i++) {
    // TODO: ERROR ON NON-NUMBER
    if (!IS_NUMBER(args[i])) {
      PANIC("Object is not a number: %d\n", AS_OBJECT(args[i])->kind);
    }

    result += AS_NUMBER(args[i]);
  }

  return NUMBER_VAL(result);
}

Value core_minus_msc(VM *vm, int arg_count, Value *args) {
  // TODO: USE NORMAL ERROR
  if (arg_count < 1) {
    PANIC("Procedure `-` requires at least one argument");
  }

  if (arg_count == 1) {
    // Special case, single argument should be zero
    return NUMBER_VAL(0 - AS_NUMBER(args[0]));
  }

  double result = AS_NUMBER(args[0]);
  for (int i = 1; i < arg_count; i++) {
    // TODO: ERROR ON NON-NUMBER
    if (!IS_NUMBER(args[i])) {
      PANIC("Object is not a number: %d\n", AS_OBJECT(args[i])->kind);
    }

    result -= AS_NUMBER(args[i]);
  }

  return NUMBER_VAL(result);
}

Value core_multiply_msc(VM *vm, int arg_count, Value *args) {
  double result = 1.f;
  for (int i = 0; i < arg_count; i++) {
    // TODO: ERROR ON NON-NUMBER
    if (!IS_NUMBER(args[i])) {
      PANIC("Object is not a number: %d\n", AS_OBJECT(args[i])->kind);
    }

    result *= AS_NUMBER(args[i]);
  }

  return NUMBER_VAL(result);
}

Value core_divide_msc(VM *vm, int arg_count, Value *args) {
  // TODO: USE NORMAL ERROR
  if (arg_count < 1) {
    PANIC("Procedure `-` requires at least one argument");
  }

  if (arg_count == 1) {
    // Special case, single argument should be inverted
    return NUMBER_VAL(1 / AS_NUMBER(args[0]));
  }

  double result = AS_NUMBER(args[0]);
  for (int i = 1; i < arg_count; i++) {
    // TODO: ERROR ON NON-NUMBER
    if (!IS_NUMBER(args[i])) {
      PANIC("Object is not a number: %d\n", AS_OBJECT(args[i])->kind);
    }

    result = result / AS_NUMBER(args[i]);
  }

  return NUMBER_VAL(result);
}

Value core_symbol_to_string_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires 1 parameter.");
  }

  ObjectSymbol *symbol = AS_SYMBOL(args[0]);
  return OBJECT_VAL(symbol->name);
}

Value core_string_to_symbol_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires 1 parameter.");
  }

  ObjectString *string = AS_STRING(args[0]);
  return OBJECT_VAL(mesche_object_make_symbol(vm, string->chars, string->length));
}

Value core_display_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires 1 parameter.");
  }

  // Print the value
  // TODO: Use parameterized current-output-port!
  mesche_value_print(vm->output_port, args[0]);

  return UNSPECIFIED_VAL;
}

Value core_add_to_load_path_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires 1 parameter.");
  }

  // Add the specified path to the load path
  mesche_vm_load_path_add(vm, AS_CSTRING(args[0]));

  return TRUE_VAL;
}

void mesche_core_module_init(VM *vm) {
  mesche_vm_define_native_funcs(
      vm, "mesche core",
      (MescheNativeFuncDetails[]){{"number?", core_number_p_msc, true},
                                  {"boolean?", core_boolean_p_msc, true},
                                  {"pair?", core_pair_p_msc, true},
                                  {"string?", core_string_p_msc, true},
                                  {"symbol?", core_symbol_p_msc, true},
                                  {"keyword?", core_keyword_p_msc, true},
                                  {"array?", core_array_p_msc, true},
                                  {"function?", core_function_p_msc, true},
                                  {"equal?", core_equal_p_msc, true},
                                  {"eqv?", core_eqv_p_msc, true},
                                  {"not", core_not_msc, true},
                                  {"cons", core_cons_msc, true},
                                  {"list", core_list_msc, true},
                                  {"car", core_car_msc, true},
                                  {"cdr", core_cdr_msc, true},
                                  {"cadr", core_car_msc, true},
                                  {"append", core_append_msc, true},
                                  {"+", core_plus_msc, true},
                                  {"-", core_minus_msc, true},
                                  {"*", core_multiply_msc, true},
                                  {"/", core_divide_msc, true},
                                  {"symbol->string", core_symbol_to_string_msc, true},
                                  {"string->symbol", core_string_to_symbol_msc, true},
                                  {"display", core_display_msc, true},
                                  {"add-to-load-path", core_add_to_load_path_msc, true},
                                  {NULL, NULL, false}});
}
