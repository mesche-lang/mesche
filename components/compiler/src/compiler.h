#ifndef mesche_compiler_h
#define mesche_compiler_h

#include "function.h"
#include "module.h"
#include "reader.h"
#include "vm.h"

ObjectFunction *mesche_compile_source(VM *vm, Reader *reader);
ObjectModule *mesche_compile_module(VM *vm, ObjectModule *module, Reader *reader);
void mesche_compiler_mark_roots(void *target);

#endif