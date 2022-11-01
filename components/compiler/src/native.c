#include <stdlib.h>

#include "mem.h"
#include "native.h"

ObjectNativeFunction *mesche_object_make_native_function(VM *vm, FunctionPtr function) {
  ObjectNativeFunction *native = ALLOC_OBJECT(vm, ObjectNativeFunction, ObjectKindNativeFunction);
  native->function = function;

  return native;
}

void mesche_free_native_function(VM *vm, ObjectNativeFunction *function) {
  FREE(vm, ObjectNativeFunction, function);
}

ObjectPointer *mesche_object_make_pointer(VM *vm, void *ptr, bool is_managed) {
  ObjectPointer *pointer = ALLOC_OBJECT(vm, ObjectPointer, ObjectKindPointer);
  pointer->ptr = ptr;
  pointer->is_managed = is_managed;
  pointer->type = NULL;

  return pointer;
}

void mesche_free_pointer(VM *vm, ObjectPointer *pointer) {
  if (pointer->ptr != NULL && pointer->is_managed) {
    // Call the pointer's own free function if there is one
    if (pointer->type != NULL && pointer->type->free_func != NULL) {
      pointer->type->free_func((MescheMemory *)vm, pointer->ptr);
    } else {
      free(pointer->ptr);
    }

    pointer->ptr = NULL;
  }

  FREE(vm, ObjectPointer, pointer);
}

ObjectPointer *mesche_object_make_pointer_type(VM *vm, void *ptr, ObjectPointerType *type) {
  ObjectPointer *pointer = ALLOC_OBJECT(vm, ObjectPointer, ObjectKindPointer);
  pointer->ptr = ptr;
  pointer->is_managed = true;
  pointer->type = type;

  return pointer;
}
