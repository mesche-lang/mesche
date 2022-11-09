#ifndef mesche_vm_h
#define mesche_vm_h

#include "value.h"

typedef struct VM VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR,
} InterpretResult;

void mesche_vm_init(VM *vm, int arg_count, char **arg_array);
void mesche_vm_free(VM *vm);
void mesche_vm_stack_push(VM *vm, Value value);
Value mesche_vm_stack_pop(VM *vm);
void mesche_vm_raise_error(VM *vm, const char *format, ...);
InterpretResult mesche_vm_run(VM *vm);
InterpretResult mesche_vm_eval_string(VM *vm, const char *script_string);
InterpretResult mesche_vm_load_file(VM *vm, const char *file_path);
void mesche_vm_register_core_modules(VM *vm, char *module_path);
void mesche_vm_load_path_add(VM *vm, const char *load_path);

#endif
