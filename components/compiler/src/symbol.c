#include "symbol.h"
#include "vm-impl.h"

void mesche_free_symbol(VM *vm, ObjectSymbol *symbol) { FREE(vm, ObjectSymbol, symbol); }

ObjectSymbol *mesche_object_make_symbol(VM *vm, const char *chars, int length) {
  // Is the symbol name already interned?
  uint32_t hash = mesche_string_hash(chars, length);
  ObjectString *symbol_name =
      (ObjectString *)mesche_table_find_key(&vm->symbols, chars, length, hash);

  // If the symbol's interned name string was not found, create a new one
  if (symbol_name == NULL) {
    symbol_name = mesche_object_make_string(vm, chars, length);
  }

  // Push the string temporarily to avoid GC
  mesche_vm_stack_push(vm, OBJECT_VAL(symbol_name));

  // Allocate and initialize the string object
  ObjectSymbol *symbol = ALLOC_OBJECT(vm, ObjectSymbol, ObjectKindSymbol);
  symbol->name = symbol_name;
  symbol->token_kind = TokenKindSymbol;

  // Pop the name string and push the symbol onto the stack temporarily to avoid GC
  mesche_vm_stack_pop(vm);
  mesche_vm_stack_push(vm, OBJECT_VAL(symbol));

  // Add the symbol's string to the interned set.  This will allow us to use the
  // same string later to make symbol name comparisons more efficient.  Note
  // that we don't store the symbol itself because there can be many source locations
  // where a symbol of the same name exists!
  mesche_table_set((MescheMemory *)vm, &vm->symbols, symbol->name, FALSE_VAL);

  // Pop the symbol back off the stack
  mesche_vm_stack_pop(vm);

  return symbol;
}
