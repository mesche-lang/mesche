#include <stdlib.h>
#include <string.h>

#include "io.h"
#include "native.h"
#include "object.h"
#include "port.h"
#include "value.h"
#include "vm-impl.h"

MeschePort *mesche_io_make_port(VM *vm, MeschePortKind kind, FILE *fp, ObjectString *name) {
  MeschePort *port = ALLOC_OBJECT(vm, MeschePort, ObjectKindPort);
  port->name = name;
  port->kind = kind;
  port->fp = fp;
  port->can_close = true;
  port->is_closed = false;

  return port;
}

MeschePort *mesche_free_port(VM *vm, MeschePort *port) {
  // If the port is closeable and not yet closed, do it
  if (port->can_close && !port->is_closed) {
    fclose(port->fp);
  }

  FREE(vm, MeschePort, port);
}

void mesche_io_port_print(MeschePort *output_port, MeschePort *port, MeschePrintStyle style) {
  fputs("#<", output_port->fp);
  if (port->is_closed) {
    fputs("closed: ", output_port->fp);
  } else {
    switch (port->kind) {
    case MeschePortKindInput:
      fputs("input: ", output_port->fp);
      break;
    case MeschePortKindOutput:
      fputs("output: ", output_port->fp);
      break;
    }

    fprintf(output_port->fp, "file %s", port->name ? port->name->chars : "(unnamed)");
  }
  fputs(">", output_port->fp);
}

#define CHECK_PORT(port, expected_kind)                                                            \
  if (port == NULL) {                                                                              \
    mesche_vm_raise_error(vm, "Received NULL port.");                                              \
  } else if (port->is_closed) {                                                                    \
    mesche_vm_raise_error(vm, "Cannot read from closed port.");                                    \
  } else if (port->kind != MeschePortKindInput) {                                                  \
    mesche_vm_raise_error(vm, "Text can only be read from a textual input port.");                 \
  }

Value read_all_text_msc(VM *vm, int arg_count, Value *args) {
  MeschePort *port = NULL;
  EXPECT_ARG_COUNT(1);
  EXPECT_OBJECT_KIND(ObjectKindPort, 0, AS_PORT, port);
  CHECK_PORT(port, MeschePortKindInput);

  if (port->kind != MeschePortKindInput) {
    mesche_vm_raise_error(vm, "read-all-text: Can only read from textual input port.");
  }

  int buffer_size = 1024;
  char *buffer = malloc(sizeof(char) * buffer_size);

  int length = 0;
  int read_count = 0;
  int read_limit = buffer_size - 1;
  while ((read_count = fread(buffer + length, sizeof(char), read_limit, port->fp))) {
    int prev_length = length;
    length += read_count;
    read_limit = (buffer_size - length - 1);
    if (feof(port->fp) == 0 && read_limit <= 0) {
      // Increase the size of the buffer
      buffer_size = buffer_size * 2;
      buffer = realloc(buffer, buffer_size + 1);
      read_limit = buffer_size - length - 1;
    }
  }

  ObjectString *contents_string = mesche_object_make_string(vm, buffer, length);
  free(buffer);

  return OBJECT_VAL(contents_string);
}

Value open_input_file_msc(VM *vm, int arg_count, Value *args) {
  ObjectString *file_path = NULL;
  EXPECT_ARG_COUNT(1);
  EXPECT_OBJECT_KIND(ObjectKindString, 0, AS_STRING, file_path);

  FILE *file = fopen(file_path->chars, "r");
  if (file == NULL) {
    // What kind of error was raised?
    mesche_vm_raise_error(vm, "File could not be opened due to error: %s", file_path->chars);
  }

  return OBJECT_VAL(mesche_io_make_port(vm, MeschePortKindInput, file, file_path));
}

Value open_output_file_msc(VM *vm, int arg_count, Value *args) {
  ObjectString *file_path = NULL;
  EXPECT_ARG_COUNT(1);
  EXPECT_OBJECT_KIND(ObjectKindString, 0, AS_STRING, file_path);

  FILE *file = fopen(file_path->chars, "w");
  if (file == NULL) {
    // What kind of error was raised?
    mesche_vm_raise_error(vm, "File could not be opened due to error: %s", file_path->chars);
  }

  return OBJECT_VAL(mesche_io_make_port(vm, MeschePortKindInput, file, file_path));
}

Value close_port_internal(MeschePort *port) {
  if (!port->can_close) {
    // TODO: Error?
    return FALSE_VAL;
  } else {
    if (port->is_closed) {
      return FALSE_VAL;
    }

    // Close the underlying file descriptor
    fclose(port->fp);
  }

  return TRUE_VAL;
}

Value close_port_msc(VM *vm, int arg_count, Value *args) {
  MeschePort *port = NULL;
  EXPECT_ARG_COUNT(1);
  EXPECT_OBJECT_KIND(ObjectKindPort, 0, AS_PORT, port);

  return close_port_internal(port);
}

Value close_input_port_msc(VM *vm, int arg_count, Value *args) {
  MeschePort *port = NULL;
  EXPECT_ARG_COUNT(1);
  EXPECT_OBJECT_KIND(ObjectKindPort, 0, AS_PORT, port);

  if (port->kind != MeschePortKindInput) {
    mesche_vm_raise_error(vm, "close-input-port: Cannot close output port.");
  }

  return close_port_internal(port);
}

Value close_output_port_msc(VM *vm, int arg_count, Value *args) {
  MeschePort *port = NULL;
  EXPECT_ARG_COUNT(1);
  EXPECT_OBJECT_KIND(ObjectKindPort, 0, AS_PORT, port);

  if (port->kind != MeschePortKindOutput) {
    mesche_vm_raise_error(vm, "close-output-port: Cannot close input port.");
  }

  return close_port_internal(port);
}

void mesche_io_module_init(VM *vm) {
  mesche_vm_define_native_funcs(
      vm, "mesche io",
      (MescheNativeFuncDetails[]){{"open-input-file", open_input_file_msc, true},
                                  {"open-output-file", open_output_file_msc, true},
                                  {"close_port", close_port_msc, true},
                                  {"close-input-port", close_input_port_msc, true},
                                  {"close-output-port", close_output_port_msc, true},
                                  {"read-all-text", read_all_text_msc, true},
                                  {NULL, NULL, false}});
}
