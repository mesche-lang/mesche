#ifndef mesche_symbol_h
#define mesche_symbol_h

#include "object.h"
#include "scanner.h"
#include "string.h"
#include "vm.h"

typedef struct ObjectSymbol {
  struct Object object;
  TokenKind token_kind;
  ObjectString *name;
} ObjectSymbol;

#define IS_SYMBOL(value) mesche_object_is_kind(value, ObjectKindSymbol)
#define AS_SYMBOL(value) ((ObjectSymbol *)AS_OBJECT(value))

ObjectSymbol *mesche_object_make_symbol(VM *vm, const char *chars, int length);
void mesche_free_symbol(VM *vm, ObjectSymbol *symbol);

#endif
