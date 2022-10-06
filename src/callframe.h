#ifndef mesche_call_frame_h
#define mesche_call_frame_h

#include <stdint.h>

#include "closure.h"
#include "value.h"

typedef struct {
  ObjectClosure *closure;
  uint8_t *ip;
  Value *slots;
  int total_arg_count;
} CallFrame;

#endif
