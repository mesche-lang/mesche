#ifndef mesche_port_h
#define mesche_port_h

#include "object.h"
#include "string.h"

typedef struct MeschePort {
  Object object;
  MeschePortKind kind;
  ObjectString *name;
  FILE *fp;
  bool can_close;
  bool is_closed;
} MeschePort;

#define IS_PORT(value) mesche_object_is_kind(value, ObjectKindPort)
#define AS_PORT(value) ((MeschePort *)AS_OBJECT(value))

MeschePort *mesche_io_make_port(VM *vm, MeschePortKind kind, FILE *fp, ObjectString *name);
MeschePort *mesche_free_port(VM *vm, MeschePort *port);

#endif
