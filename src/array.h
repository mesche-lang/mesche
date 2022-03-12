#ifndef mesche_array_h
#define mesche_array_h

#include "object.h"
#include "vm.h"

Value mesche_array_push(MescheMemory *mem, ObjectArray *array, Value value);
void mesche_array_module_init(VM *vm);

#endif
