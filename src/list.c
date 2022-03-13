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
  ObjectCons *current_cons = AS_CONS(args[0]);
  return current_cons->car;
}

Value list_cdr_msc(MescheMemory *mem, int arg_count, Value *args) {
  ObjectCons *current_cons = AS_CONS(args[0]);
  return current_cons->cdr;
}

Value list_pair_p_msc(MescheMemory *mem, int arg_count, Value *args) {
  ObjectCons *current_cons = AS_CONS(args[0]);
  return BOOL_VAL(mesche_object_is_kind(args[0], ObjectKindCons));
}

void mesche_list_module_init(VM *vm) {
  mesche_vm_define_native_funcs(vm, "mesche list",
                                &(MescheNativeFuncDetails[]){{"list-nth", list_nth_msc, true},
                                                             {"pair?", list_pair_p_msc, true},
                                                             {"car", list_car_msc, true},
                                                             {"cdr", list_cdr_msc, true},
                                                             {NULL, NULL, false}});
}
