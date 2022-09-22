#include "list.h"
#include "object.h"
#include "util.h"

ObjectCons *mesche_list_push(VM *vm, ObjectCons *list, Value value) {
  // Create a new cons to wrap the previous list
  return mesche_object_make_cons(vm, value, list != NULL ? OBJECT_VAL(list) : EMPTY_VAL);
}

Value mesche_list_nth(VM *vm, ObjectCons *list, int index) {
  Value current = OBJECT_VAL(list);
  for (int i = 0; IS_CONS(current); i++) {
    ObjectCons *current_cons = AS_CONS(current);
    if (i == index) {
      return current_cons->car;
    }
    current = current_cons->cdr;
  }

  return NIL_VAL;
}

Value list_nth_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (arg_count != 2) {
    PANIC("Function requires 2 parameters.");
  }

  return mesche_list_nth((VM *)mem, AS_CONS(args[0]), (int)AS_NUMBER(args[1]));
}

Value list_append_msc(MescheMemory *mem, int arg_count, Value *args) {
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

void mesche_list_module_init(VM *vm) {
  mesche_vm_define_native_funcs(vm, "mesche list",
                                (MescheNativeFuncDetails[]){{"list-nth", list_nth_msc, true},
                                                            {"append", list_append_msc, true},
                                                            {NULL, NULL, false}});
}
