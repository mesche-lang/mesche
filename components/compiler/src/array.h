#ifndef mesche_array_h
#define mesche_array_h

#include "object.h"
#include "vm.h"

typedef struct ObjectArray {
  struct Object object;
  ValueArray objects;
} ObjectArray;

#define IS_ARRAY(value) mesche_object_is_kind(value, ObjectKindArray)
#define AS_ARRAY(value) ((ObjectArray *)AS_OBJECT(value))

ObjectArray *mesche_object_make_array(VM *vm);
void mesche_free_array(VM *vm, ObjectArray *array);
Value mesche_array_push(MescheMemory *mem, ObjectArray *array, Value value);
void mesche_array_module_init(VM *vm);

#endif
