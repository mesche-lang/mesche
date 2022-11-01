#ifndef mesche_string_h
#define mesche_string_h

#include "object.h"

typedef struct ObjectString {
  struct Object object;
  uint32_t hash;
  int length;
  char chars[];
} ObjectString;

ObjectString *mesche_object_make_string(VM *vm, const char *chars, int length);
void mesche_free_string(VM *vm, ObjectString *string);

uint32_t mesche_string_hash(const char *key, int length);

char *mesche_cstring_join(const char *left, size_t left_length, const char *right,
                          size_t right_length, const char *separator);
ObjectString *mesche_string_join(VM *vm, ObjectString *left, ObjectString *right,
                                 const char *separator);

void mesche_string_module_init(VM *vm);

#define IS_STRING(value) mesche_object_is_kind(value, ObjectKindString)
#define AS_STRING(value) ((ObjectString *)AS_OBJECT(value))
#define AS_CSTRING(value) (((ObjectString *)AS_OBJECT(value))->chars)

#endif
