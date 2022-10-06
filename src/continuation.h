#ifndef mesche_continuation_h
#define mesche_continuation_h

#include "callframe.h"

typedef struct ObjectContinuation {
  Object object;
  CallFrame *frames;
  uint8_t frame_count;

  Value *stack;
  uint8_t stack_count;
} ObjectContinuation;

#define IS_CONTINUATION(value) mesche_object_is_kind(value, ObjectKindContinuation)
#define AS_CONTINUATION(value) ((ObjectContinuation *)AS_OBJECT(value))

ObjectContinuation *mesche_object_make_continuation(VM *vm, CallFrame *frame_start,
                                                    uint8_t frame_count, Value *stack_start,
                                                    uint8_t stack_count);
void mesche_free_continuation(VM *vm, ObjectContinuation *continuation);

#endif
