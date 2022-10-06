#include "closure.h"
#include "mem.h"

#define ALLOC(vm, type, count)                                                                     \
  (type) mesche_mem_realloc((MescheMemory *)vm, NULL, 0, sizeof(type) * count)

ObjectClosure *mesche_object_make_closure(VM *vm, ObjectFunction *function, ObjectModule *module) {
  // Allocate upvalues for this closure instance
  ObjectUpvalue **upvalues = ALLOC(vm, ObjectUpvalue **, function->upvalue_count);
  for (int i = 0; i < function->upvalue_count; i++) {
    upvalues[i] = NULL;
  }

  ObjectClosure *closure = ALLOC_OBJECT(vm, ObjectClosure, ObjectKindClosure);
  closure->function = function;
  closure->module = module;
  closure->upvalues = upvalues;
  closure->upvalue_count = function->upvalue_count;

  return closure;
};

void mesche_free_closure(VM *vm, ObjectClosure *closure) {
  FREE_ARRAY(vm, ObjectUpvalue *, closure->upvalues, closure->upvalue_count);
  FREE(vm, ObjectClosure, closure);
}

ObjectUpvalue *mesche_object_make_upvalue(VM *vm, Value *slot) {
  ObjectUpvalue *upvalue = ALLOC_OBJECT(vm, ObjectUpvalue, ObjectKindUpvalue);
  upvalue->location = slot;
  upvalue->next = NULL;
  upvalue->closed = FALSE_VAL;

  return upvalue;
}

void mesche_free_upvalue(VM *vm, ObjectUpvalue *upvalue) { FREE(vm, ObjectUpvalue, upvalue); }
