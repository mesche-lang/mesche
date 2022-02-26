#ifndef mesche_list_h
#define mesche_list_h

#include "vm.h"

ObjectCons *mesche_list_push(VM *vm, ObjectCons *list, Value value);
Value mesche_list_nth(VM *vm, ObjectCons *list, int index);
Value mesche_list_nth_msc(MescheMemory *mem, int arg_count, Value *args);

#endif
