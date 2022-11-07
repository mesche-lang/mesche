#include "keyword.h"
#include "vm-impl.h"

ObjectKeyword *mesche_object_make_keyword(VM *vm, const char *chars, int length) {
  // Is the keyword already interned?
  uint32_t hash = mesche_string_hash(chars, length);
  ObjectKeyword *keyword =
      (ObjectKeyword *)mesche_table_find_key(&vm->keywords, chars, length, hash);

  // If the keyword's interned string was not found, create a new one
  if (keyword == NULL) {
    // Allocate the keyword object
    keyword = (ObjectKeyword *)ALLOC_OBJECT_EX(vm, ObjectKeyword, length + 1, ObjectKindKeyword);
    memcpy(keyword->string.chars, chars, length);
    keyword->string.chars[length] = '\0';
    keyword->string.length = length;
    keyword->string.hash = hash;

    // Push the keyword temporarily to avoid GC
    mesche_vm_stack_push(vm, OBJECT_VAL(keyword));

    // Add the keyword to the interned set
    mesche_table_set((MescheMemory *)vm, &vm->keywords, (ObjectString *)keyword, FALSE_VAL);

    // Pop the keyword back off the stack
    mesche_vm_stack_pop(vm);
  }

  return keyword;
}

void mesche_free_keyword(VM *vm, ObjectKeyword *keyword) {
  FREE_SIZE(vm, keyword, (sizeof(ObjectKeyword) + keyword->string.length + 1));
}
