#ifndef mesche_compiler_h
#define mesche_compiler_h

#include "scanner.h"
#include "vm.h"

ObjectFunction *mesche_compile_source(VM *vm, const char *script_source);
ObjectModule *mesche_compile_module(VM *vm, ObjectModule *module, const char *module_source);
void mesche_compiler_mark_roots(void *target);

#endif
