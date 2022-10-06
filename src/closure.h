#ifndef mesche_closure_h
#define mesche_closure_h

#include "function.h"
#include "module.h"
#include "object.h"
#include "value.h"

#define IS_CLOSURE(value) mesche_object_is_kind(value, ObjectKindClosure)
#define AS_CLOSURE(value) ((ObjectClosure *)AS_OBJECT(value))

typedef struct ObjectUpvalue {
  Object object;
  Value *location;
  Value closed;
  struct ObjectUpvalue *next;
} ObjectUpvalue;

typedef struct ObjectClosure {
  Object object;
  ObjectModule *module;
  ObjectFunction *function;
  ObjectUpvalue **upvalues;
  int upvalue_count;
} ObjectClosure;

ObjectUpvalue *mesche_object_make_upvalue(VM *vm, Value *slot);
void mesche_free_upvalue(VM *vm, ObjectUpvalue *upvalue);

ObjectClosure *mesche_object_make_closure(VM *vm, ObjectFunction *function, ObjectModule *module);
void mesche_free_closure(VM *vm, ObjectClosure *closure);

#endif
