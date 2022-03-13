#ifndef mesche_string_h
#define mesche_string_h

#include "object.h"

char *mesche_cstring_join(const char *left, size_t left_length, const char *right,
                          size_t right_length, const char *separator);
ObjectString *mesche_string_join(VM *vm, ObjectString *left, ObjectString *right,
                                 const char *separator);

void mesche_string_module_init(VM *vm);

#endif
