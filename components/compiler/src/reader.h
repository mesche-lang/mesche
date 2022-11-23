#ifndef mesche_reader_h
#define mesche_reader_h

#include "scanner.h"
#include "string.h"
#include "syntax.h"
#include "value.h"
#include "vm.h"

typedef struct Reader {
  VM *vm;
  Scanner scanner;
  ObjectString *file_name;
} Reader;

void mesche_reader_init(Reader *reader, struct VM *vm, MeschePort *port, ObjectString *file_name);
Value mesche_reader_read_next(Reader *reader);
void mesche_reader_module_init(VM *vm);

#endif
