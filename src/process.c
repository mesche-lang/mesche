#include "object.h"
#include "process.h"
#include "util.h"

Value mesche_process_arguments_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (arg_count != 0) {
    PANIC("Function does not accept arguments.");
  }

  VM *vm = (VM *)mem;
  Value first = EMPTY_VAL;
  for (int i = vm->arg_count - 1; i >= 0; i--) {
    ObjectString *arg_string =
        mesche_object_make_string(vm, vm->arg_array[i], strlen(vm->arg_array[i]));
    first = OBJECT_VAL(mesche_object_make_cons(vm, OBJECT_VAL(arg_string), first));
  }

  return first;
}
