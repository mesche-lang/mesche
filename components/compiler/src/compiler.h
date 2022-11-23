#ifndef mesche_compiler_h
#define mesche_compiler_h

#include "function.h"
#include "module.h"
#include "reader.h"
#include "vm.h"

Value mesche_compile_source(VM *vm, Reader *reader);
Value mesche_compile_module(VM *vm, ObjectModule *module, Reader *reader);
void mesche_compiler_mark_roots(void *target);
void mesche_compiler_module_init(VM *vm);

#endif
