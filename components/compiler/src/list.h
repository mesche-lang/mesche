#ifndef mesche_list_h
#define mesche_list_h

#include "object.h"
#include "vm.h"

ObjectCons *mesche_list_push(VM *vm, ObjectCons *list, Value value);
Value mesche_list_nth(VM *vm, ObjectCons *list, int index);
void mesche_list_module_init(VM *vm);

#endif
