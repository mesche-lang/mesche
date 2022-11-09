#ifndef mesche_port_h
#define mesche_port_h

#include "object.h"
#include "string.h"

#define INITIAL_STRING_PORT_SIZE 128

typedef struct {
  char *buffer;
  int size;
  int index;
} MescheStringPortData;

typedef struct {
  char *name;
  FILE *fp;
} MescheFilePortData;

typedef enum {
  MeschePortDataKindFile,
  MeschePortDataKindString,
  MeschePortDataKindBinaryFile
} MeschePortDataKind;

typedef struct MeschePort {
  Object object;
  MeschePortKind kind;

  MeschePortDataKind data_kind;
  union MeschePortData {
    MescheStringPortData string;
    MescheFilePortData file;
  } data;

  char peek_byte;
  bool is_peeked;
  bool can_close;
  bool is_closed;
} MeschePort;

#define IS_PORT(value) mesche_object_is_kind(value, ObjectKindPort)
#define AS_PORT(value) ((MeschePort *)AS_OBJECT(value))

// TODO: Decide on whether it's port or io

Value mesche_io_make_string_port(VM *vm, MeschePortKind kind, char *input_string, int length);
Value mesche_io_make_file_port(VM *vm, MeschePortKind kind, FILE *fp, char *name, int flags);
Value mesche_io_make_file_port_from_path(VM *vm, MeschePortKind kind, char *file_path, int flags);
Value mesche_port_close(VM *vm, MeschePort *port);

Value mesche_port_read_char(VM *vm, MeschePort *port);
Value mesche_port_read_string(VM *vm, MeschePort *port);
Value mesche_port_write_char(VM *vm, MeschePort *port, char c);
Value mesche_port_write_string(VM *vm, MeschePort *port, ObjectString *string, int start, int end);
Value mesche_port_write_cstring(VM *vm, MeschePort *port, char *string, int count);

Value mesche_port_output_string(VM *vm, MeschePort *port);

void mesche_free_port(VM *vm, MeschePort *port);

#endif
