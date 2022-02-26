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

Value mesche_list_nth_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (arg_count != 2) {
    PANIC("Function requires 2 parameters.");
  }

  return mesche_list_nth((VM *)mem, AS_CONS(args[0]), (int)AS_NUMBER(args[1]));
}
