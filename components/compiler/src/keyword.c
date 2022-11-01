#include "keyword.h"

ObjectKeyword *mesche_object_make_keyword(VM *vm, const char *chars, int length) {
  // TODO: Do we want to intern keyword strings too?
  // Allocate and initialize the string object
  uint32_t hash = mesche_string_hash(chars, length);
  ObjectKeyword *keyword = ALLOC_OBJECT_EX(vm, ObjectKeyword, length + 1, ObjectKindKeyword);
  memcpy(keyword->string.chars, chars, length);
  keyword->string.chars[length] = '\0';
  keyword->string.length = length;
  keyword->string.hash = hash;

  return (ObjectKeyword *)keyword;
}

void mesche_free_keyword(VM *vm, ObjectKeyword *keyword) {
  FREE_SIZE(vm, keyword, (sizeof(ObjectKeyword) + keyword->string.length + 1));
}
