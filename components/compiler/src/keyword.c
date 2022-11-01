#include "keyword.h"
#include "vm-impl.h"

ObjectKeyword *mesche_object_make_keyword(VM *vm, const char *chars, int length) {
  // Is the keyword already interned?
  uint32_t hash = mesche_string_hash(chars, length);
  ObjectString *keyword = (ObjectString *)mesche_table_find_key(&vm->keywords, chars, length, hash);

  // If the keyword's interned string was not found, create a new one
  if (keyword == NULL) {
    // Allocate the keyword object
    keyword = (ObjectString *)ALLOC_OBJECT_EX(vm, ObjectKeyword, length + 1, ObjectKindKeyword);
    memcpy(keyword->chars, chars, length);
    keyword->chars[length] = '\0';
    keyword->length = length;
    keyword->hash = hash;

    // Add the keyword to the interned set
    // TODO: Use an API for this!
    mesche_table_set((MescheMemory *)vm, &vm->keywords, keyword, FALSE_VAL);
  }

  // Allocate and initialize the string object
  return (ObjectKeyword *)keyword;
}

void mesche_free_keyword(VM *vm, ObjectKeyword *keyword) {
  FREE_SIZE(vm, keyword, (sizeof(ObjectKeyword) + keyword->string.length + 1));
}
