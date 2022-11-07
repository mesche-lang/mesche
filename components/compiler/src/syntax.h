#ifndef mesche_syntax_h
#define mesche_syntax_h

#include <stdint.h>

#include "io.h"
#include "object.h"
#include "string.h"
#include "vm.h"

typedef struct ObjectSyntax {
  struct Object object;
  uint32_t line;
  uint32_t column;
  uint32_t position;
  uint32_t span;
  ObjectString *file_name;
  Value value;
} ObjectSyntax;

#define IS_SYNTAX(value) mesche_object_is_kind(value, ObjectKindSyntax)
#define AS_SYNTAX(value) ((ObjectSyntax *)AS_OBJECT(value))

void mesche_syntax_module_init(VM *vm);

ObjectSyntax *mesche_object_make_syntax(VM *vm, uint32_t line, uint32_t column, uint32_t position,
                                        uint32_t span, ObjectString *file_name, Value value);

void mesche_free_syntax(VM *vm, ObjectSyntax *syntax);

void mesche_syntax_print(MeschePort *port, ObjectSyntax *syntax);
void mesche_syntax_print_ex(MeschePort *port, ObjectSyntax *syntax, MeschePrintStyle style);
Value mesche_syntax_to_datum(VM *vm, Value syntax);

#endif
