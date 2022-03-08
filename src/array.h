#ifndef mesche_array_h
#define mesche_array_h

#include "object.h"
#include "vm.h"

Value mesche_array_push(MescheMemory *mem, ObjectArray *array, Value value);
Value mesche_array_make_msc(MescheMemory *mem, int arg_count, Value *args);
Value mesche_array_push_msc(MescheMemory *mem, int arg_count, Value *args);
Value mesche_array_length_msc(MescheMemory *mem, int arg_count, Value *args);
Value mesche_array_nth_msc(MescheMemory *mem, int arg_count, Value *args);

#endif
