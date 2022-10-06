#ifndef mesche_keyword_h
#define mesche_keyword_h

#include "string.h"

typedef struct ObjectKeyword {
  // A keyword is basically a tagged string
  ObjectString string;
} ObjectKeyword;

#define IS_KEYWORD(value) mesche_object_is_kind(value, ObjectKindKeyword)
#define AS_KEYWORD(value) ((ObjectKeyword *)AS_OBJECT(value))

ObjectKeyword *mesche_object_make_keyword(VM *vm, const char *chars, int length);
void mesche_free_keyword(VM *vm, ObjectKeyword *keyword);

#endif
