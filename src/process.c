#include <stdio.h>
#include <unistd.h>

#include "object.h"
#include "process.h"
#include "util.h"

Value process_arguments_msc(MescheMemory *mem, int arg_count, Value *args) {
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

Value process_directory_msc(MescheMemory *mem, int arg_count, Value *args) {
  char cwd[512];
  char *current_path = getcwd(cwd, sizeof(cwd));
  return OBJECT_VAL(mesche_object_make_string(mem, current_path, strlen(current_path)));
}

Value process_directory_set_msc(MescheMemory *mem, int arg_count, Value *args) {
  chdir(AS_CSTRING(args[0])) == 0 ? T_VAL: NIL_VAL;
}

Value process_start_msc(MescheMemory *mem, int arg_count, Value *args) {
  ObjectString *process_string = AS_STRING(args[0]);
  FILE *process_pipe = popen(process_string->chars, "r");

  char c;
  while(c = getc(process_pipe) != EOF) {
    putchar(c);
  }

  return T_VAL;
}

void mesche_process_module_init(VM *vm) {
  mesche_vm_define_native_funcs(
      vm, "mesche process",
      &(MescheNativeFuncDetails[]){{"process-start", process_start_msc, true},
                                   {"process-arguments", process_arguments_msc, true},
                                   {"process-directory", process_directory_msc, true},
                                   {"process-directory-set!", process_directory_set_msc, true},
                                   {NULL, NULL, false}});
}
