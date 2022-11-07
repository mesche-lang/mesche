#ifndef mesche_native_h
#define mesche_native_h

#include "gc.h"
#include "module.h"
#include "object.h"

typedef Value (*FunctionPtr)(VM *vm, int arg_count, Value *args);

typedef struct {
  Object object;
  FunctionPtr function;
} ObjectNativeFunction;

typedef struct {
  const char *name;
  ObjectFreePtr free_func;
  ObjectMarkFuncPtr mark_func;
} ObjectPointerType;

typedef struct {
  Object object;
  bool is_managed;
  void *ptr;
  const ObjectPointerType *type;
} ObjectPointer;

typedef struct {
  const char *name;
  FunctionPtr function;
  bool exported;
} MescheNativeFuncDetails;

void mesche_vm_define_native(VM *vm, ObjectModule *module, const char *name, FunctionPtr function,
                             bool exported);
void mesche_vm_define_native_funcs(VM *vm, const char *module_name,
                                   MescheNativeFuncDetails *func_array);

ObjectNativeFunction *mesche_object_make_native_function(VM *vm, FunctionPtr function);
void mesche_free_native_function(VM *vm, ObjectNativeFunction *function);

ObjectPointer *mesche_object_make_pointer(VM *vm, void *ptr, bool is_managed);
void mesche_free_pointer(VM *vm, ObjectPointer *pointer);

ObjectPointer *mesche_object_make_pointer_type(VM *vm, void *ptr, const ObjectPointerType *type);
void mesche_free_pointer_type(VM *vm, ObjectPointer *pointer);

#define IS_POINTER(value) mesche_object_is_kind(value, ObjectKindPointer)
#define AS_POINTER(value) ((ObjectPointer *)AS_OBJECT(value))

#define IS_NATIVE_FUNC(value) mesche_object_is_kind(value, ObjectKindNativeFunction)
#define AS_NATIVE_FUNC(value) (((ObjectNativeFunction *)AS_OBJECT(value))->function)

#endif
