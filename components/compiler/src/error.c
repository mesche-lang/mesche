#include <stdarg.h>
#include <stdio.h>

#include "callframe.h"
#include "error.h"
#include "util.h"
#include "vm-impl.h"

#define ERROR_MAX_LENGTH 512

Value mesche_error(VM *vm, const char *format, ...) {
  int count = 0, remaining = ERROR_MAX_LENGTH;
  char buffer[ERROR_MAX_LENGTH];

#define UPDATE_COUNT(print_op)                                                                     \
  count = print_op;                                                                                \
  remaining -= count;                                                                              \
  if (remaining <= 0) {                                                                            \
    va_list args;                                                                                  \
    va_start(args, format);                                                                        \
    vfprintf(stderr, format, args);                                                                \
    va_end(args);                                                                                  \
    PANIC("Above error is too large for buffer", format);                                          \
  }

  va_list args;
  va_start(args, format);
  UPDATE_COUNT(vsnprintf(buffer, remaining, format, args));
  va_end(args);

  // Allocate the error and the string for the message
  ObjectString *message = mesche_object_make_string(vm, buffer, ERROR_MAX_LENGTH - remaining);
  mesche_vm_stack_push(vm, OBJECT_VAL(message));
  MescheError *error = ALLOC_OBJECT(vm, MescheError, ObjectKindError);
  error->message = message;
  mesche_vm_stack_pop(vm);

  return OBJECT_VAL(error);
}

void mesche_error_print(MescheError *error, MeschePort *port) { printf("ERROR\n"); }

void mesche_free_error(VM *vm, MescheError *error) { FREE(vm, MescheError, error); }
