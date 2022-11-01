#include "continuation.h"
#include "mem.h"

ObjectContinuation *mesche_object_make_continuation(VM *vm, CallFrame *frame_start,
                                                    uint8_t frame_count, Value *stack_start,
                                                    uint8_t stack_count) {
  ObjectContinuation *continuation = ALLOC_OBJECT(vm, ObjectContinuation, ObjectKindContinuation);

  // Pre-initialize frames and values to 0 so that the arrays don't get walked
  // if GC tries to mark this object
  continuation->frame_count = 0;
  continuation->stack_count = 0;

  // Since we have to allocate arrays, temporarily push the continuation onto
  // the stack to make sure we don't free it halfway through this function
  mesche_vm_stack_push(vm, OBJECT_VAL(continuation));

  // Copy the call frames and stack values in the specified range
  continuation->frames = GROW_ARRAY((MescheMemory *)vm, CallFrame, NULL, 0, frame_count);
  continuation->frame_count = frame_count;
  memcpy(continuation->frames, frame_start, sizeof(CallFrame) * frame_count);

  continuation->stack = GROW_ARRAY((MescheMemory *)vm, Value, NULL, 0, stack_count);
  continuation->stack_count = stack_count;
  memcpy(continuation->stack, stack_start, sizeof(Value) * stack_count);

  // Pop the continuation
  mesche_vm_stack_pop(vm);

  return continuation;
}

void mesche_free_continuation(VM *vm, ObjectContinuation *continuation) {
  FREE_ARRAY(vm, CallFrame *, continuation->frames, continuation->frame_count);
  FREE_ARRAY(vm, Value *, continuation->stack, continuation->stack_count);
  FREE(vm, ObjectContinuation, continuation);
}
