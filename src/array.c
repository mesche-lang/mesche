#include "array.h"
#include "util.h"
#include "value.h"

Value mesche_array_push(MescheMemory *mem, ObjectArray *array, Value value) {
  mesche_value_array_write(mem, &array->objects, value);
  return value;
}

Value mesche_array_make_msc(MescheMemory *mem, int arg_count, Value *args) {
  ObjectArray *array = mesche_object_make_array((VM *)mem);
  return OBJECT_VAL(array);
}

Value mesche_array_push_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (arg_count != 2) {
    PANIC("Function requires 2 parameters.");
  }

  ObjectArray *array = AS_ARRAY(args[0]);
  Value value = args[1];

  mesche_array_push(mem, array, value);

  return value;
}

Value mesche_array_length_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires 1 parameter.");
  }

  ObjectArray *array = AS_ARRAY(args[0]);
  return NUMBER_VAL(array->objects.count);
}

Value mesche_array_nth_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (arg_count != 2) {
    PANIC("Function requires 2 parameters.");
  }

  ObjectArray *array = AS_ARRAY(args[0]);
  Value value = args[1];

  return array->objects.values[(int)AS_NUMBER(value)];
}
