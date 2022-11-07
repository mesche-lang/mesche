#include "array.h"
#include "native.h"
#include "util.h"
#include "value.h"

ObjectArray *mesche_object_make_array(VM *vm) {
  ObjectArray *array = ALLOC_OBJECT(vm, ObjectArray, ObjectKindArray);
  mesche_value_array_init(&array->objects);

  return array;
}

void mesche_free_array(VM *vm, ObjectArray *array) {
  mesche_value_array_free((MescheMemory *)vm, &array->objects);
  FREE(vm, ObjectArray, array);
}

Value mesche_array_push(MescheMemory *mem, ObjectArray *array, Value value) {
  mesche_value_array_write(mem, &array->objects, value);
  return value;
}

Value array_make_msc(VM *vm, int arg_count, Value *args) {
  ObjectArray *array = mesche_object_make_array(vm);

  if (arg_count == 1) {
    // Initialize the array to the specified size
    // TODO: Type check the argument
    int length = AS_NUMBER(args[0]);
    for (int i = 0; i < length; i++) {
      mesche_array_push((MescheMemory *)vm, array, FALSE_VAL);
    }
  }

  return OBJECT_VAL(array);
}

Value array_push_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 2) {
    PANIC("Function requires 2 parameters.");
  }

  ObjectArray *array = AS_ARRAY(args[0]);
  Value value = args[1];

  mesche_array_push((MescheMemory *)vm, array, value);

  return value;
}

Value array_length_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires 1 parameter.");
  }

  ObjectArray *array = AS_ARRAY(args[0]);
  return NUMBER_VAL(array->objects.count);
}

Value array_nth_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 2) {
    PANIC("Function requires 2 parameters.");
  }

  ObjectArray *array = AS_ARRAY(args[0]);
  Value value = args[1];

  return array->objects.values[(int)AS_NUMBER(value)];
}

Value array_nth_set_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 3) {
    PANIC("Function requires 3 parameters.");
  }

  ObjectArray *array = AS_ARRAY(args[0]);
  Value index = args[1];

  array->objects.values[(int)AS_NUMBER(index)] = args[2];
  return args[2];
}

void mesche_array_module_init(VM *vm) {
  mesche_vm_define_native_funcs(
      vm, "mesche array",
      (MescheNativeFuncDetails[]){{"make-array", array_make_msc, true},
                                  {"array-push", array_push_msc, true},
                                  {"array-length", array_length_msc, true},
                                  {"array-nth", array_nth_msc, true},
                                  {"array-nth-set!", array_nth_set_msc, true},
                                  {NULL, NULL, false}});
}
