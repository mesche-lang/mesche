#ifndef mesche_reader_h
#define mesche_reader_h

#include "scanner.h"
#include "string.h"
#include "syntax.h"
#include "value.h"
#include "vm.h"

typedef struct ReaderContext {
  struct VM *vm;
} ReaderContext;

typedef struct Reader {
  Scanner scanner;
  ObjectString *file_name;
  ReaderContext *context;
} Reader;

void mesche_reader_init(ReaderContext *context, struct VM *vm);
void mesche_reader_from_string(ReaderContext *context, Reader *reader, const char *source);
void mesche_reader_from_file(ReaderContext *context, Reader *reader, const char *source,
                             ObjectString *file_name);
ObjectSyntax *mesche_reader_read_next(Reader *reader);

#endif
