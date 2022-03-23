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

Value list_car_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (!IS_CONS(args[0])) {
    PANIC("Object is not a pair: %d\n", AS_OBJECT(args[0])->kind);
  }

  ObjectCons *current_cons = AS_CONS(args[0]);
  return current_cons->car;
}

Value list_cdr_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (!IS_CONS(args[0])) {
    PANIC("Object is not a pair: %d\n", AS_OBJECT(args[0])->kind);
  }

  ObjectCons *current_cons = AS_CONS(args[0]);
  return current_cons->cdr;
}

Value list_pair_p_msc(MescheMemory *mem, int arg_count, Value *args) {
  // TODO: Ensure single argument
  return BOOL_VAL(mesche_object_is_kind(args[0], ObjectKindCons));
}

Value list_append_msc(MescheMemory *mem, int arg_count, Value *args) {
  // We can just return the second parameter if the first is an empty list
  if (IS_EMPTY(args[0])) {
    return args[1];
  }

  // Loop to the end of the first cons and set its cdr to the second cons
  Value current = args[0];
  while (IS_CONS(current)) {
    ObjectCons *cons = AS_CONS(current);
    if (IS_EMPTY(cons->cdr)) {
      cons->cdr = args[1];
      break;
    }

    current = cons->cdr;
  }

  return args[0];
}

void mesche_list_module_init(VM *vm) {
  mesche_vm_define_native_funcs(vm, "mesche list",
                                (MescheNativeFuncDetails[]){{"list-nth", list_nth_msc, true},
                                                            {"pair?", list_pair_p_msc, true},
                                                            {"car", list_car_msc, true},
                                                            {"cdr", list_cdr_msc, true},
                                                            {"append", list_append_msc, true},
                                                            {NULL, NULL, false}});
}
