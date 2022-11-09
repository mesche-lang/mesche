#ifndef mesche_error_h
#define mesche_error_h

#include "object.h"
#include "string.h"
#include "value.h"

typedef struct MescheError {
  Object object;
  ObjectString *message;
} MescheError;

#define IS_ERROR(value) mesche_object_is_kind(value, ObjectKindError)
#define AS_ERROR(value) ((MescheError *)AS_OBJECT(value))

Value mesche_error(VM *vm, const char *format, ...);
void mesche_free_error(VM *vm, MescheError *error);

#endif
