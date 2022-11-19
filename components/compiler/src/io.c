#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "io.h"
#include "native.h"
#include "object.h"
#include "port.h"
#include "value.h"
#include "vm-impl.h"

#define EXPECT_OPEN_PORT(port)                                                                     \
  if (port == NULL) {                                                                              \
    return mesche_error(vm, "Port object is NULL.");                                               \
  } else if (port->is_closed) {                                                                    \
    return mesche_error(vm, "The port is already closed.");                                        \
  }

#define EXPECT_OUTPUT_PORT(port, error_msg)                                                        \
  if (port->kind != MeschePortKindOutput) {                                                        \
    return mesche_error(vm, error_msg);                                                            \
  }

#define EXPECT_INPUT_PORT(port, error_msg)                                                         \
  if (port->kind != MeschePortKindInput) {                                                         \
    return mesche_error(vm, error_msg);                                                            \
  }

#define EXPECT_FILE_PORT(port, error_msg)                                                          \
  if (port->data_kind != MeschePortDataKindFile) {                                                 \
    return mesche_error(vm, error_msg);                                                            \
  }

#define EXPECT_STRING_PORT(port, error_msg)                                                        \
  if (port->data_kind != MeschePortDataKindString) {                                               \
    return mesche_error(vm, error_msg);                                                            \
  }

#define EXPECT_BINARY_PORT(port, error_msg)                                                        \
  if (port->data_kind != MeschePortDataKindBinaryFile) {                                           \
    return mesche_error(vm, error_msg);                                                            \
  }

#define EXPECT_TEXT_PORT(port, error_msg)                                                          \
  if (port->data_kind != MeschePortDataKindFile && port->data_kind != MeschePortDataKindString) {  \
    return mesche_error(vm, error_msg);                                                            \
  }

static void port_common_init(MeschePort *port, MeschePortKind kind) {
  port->kind = kind;
  port->peek_byte = 0;
  port->is_peeked = false;
  port->can_close = true;
  port->is_closed = false;
}

Value mesche_io_make_string_port(VM *vm, MeschePortKind kind, char *input_string, int length) {
  MeschePort *port = ALLOC_OBJECT(vm, MeschePort, ObjectKindPort);
  port_common_init(port, kind);
  port->kind = kind;
  port->data_kind = MeschePortDataKindString;

  port->data.string.index = 0;
  if (input_string) {
    port->data.string.buffer = strndup(input_string, length);
    port->data.string.size = length;
  } else {
    port->data.string.buffer = NULL;
    port->data.string.size = 0;
  }

  return OBJECT_VAL(port);
}

Value mesche_io_make_file_port(VM *vm, MeschePortKind kind, FILE *fp, char *port_name, int flags) {
  MeschePort *port = ALLOC_OBJECT(vm, MeschePort, ObjectKindPort);
  mesche_vm_stack_push(vm, OBJECT_VAL(port));

  port_common_init(port, kind);
  port->kind = kind;
  port->data_kind = MeschePortDataKindFile;
  port->data.file.fp = fp;

  ObjectString *name_str = mesche_object_make_string(vm, port_name, strlen(port_name));
  port->data.file.name = name_str;

  mesche_vm_stack_pop(vm);

  return OBJECT_VAL(port);
}

Value mesche_io_make_file_port_from_path(VM *vm, MeschePortKind kind, char *file_path, int flags) {
  char *mode = "r";
  if (kind == MeschePortKindOutput) {
    mode = (flags & MeschePortFileFlagsAppend) == MeschePortFileFlagsAppend ? "a" : "w";
  }

  FILE *fp = fopen(file_path, mode);
  if (fp == NULL) {
    // What kind of error was raised?
    return mesche_error(vm, "File could not be opened due to error: %s", file_path);
  }

  return mesche_io_make_file_port(vm, kind, fp, file_path, flags);
}

Value mesche_port_close(VM *vm, MeschePort *port) {
  // If the port is closeable and not yet closed, do it
  if (port->can_close && !port->is_closed) {
    if (port->data_kind == MeschePortDataKindFile) {
      // Flush the file descriptor if it's an output port
      if (port->kind == MeschePortKindOutput) {
        fflush(port->data.file.fp);
      }

      // Close the file descriptor
      fclose(port->data.file.fp);
      port->data.file.fp = NULL;
    }

    port->is_closed = true;
    return TRUE_VAL;
  }

  return FALSE_VAL;
}

void mesche_free_port(VM *vm, MeschePort *port) {
  // First, close the port if it's already open
  mesche_port_close(vm, port);

  if (port->data_kind == MeschePortDataKindString) {
    // Deallocate the string buffer
    port->data.string.buffer = realloc(port->data.string.buffer, 0);
    port->data.string.size = 0;
    port->data.string.index = 0;
  }

  FREE(vm, MeschePort, port);
}

static void string_port_write_char(MeschePort *port, char c) {
  // Resize buffer if necessary
  if (port->data.string.size - port->data.string.index <= 1) {
    port->data.string.size =
        port->data.string.size ? port->data.string.size * 2 : INITIAL_STRING_PORT_SIZE;
    port->data.string.buffer =
        realloc(port->data.string.buffer, sizeof(char) * port->data.string.size);
  }

  // Store the character at the current string index
  port->data.string.buffer[port->data.string.index++] = c;

  // Write a null terminator so we can print the string but don't update the
  // index, we want to overwrite it on next write
  port->data.string.buffer[port->data.string.index] = '\0';
}

Value mesche_port_write_char(VM *vm, MeschePort *port, char c) {
  EXPECT_OPEN_PORT(port);
  EXPECT_TEXT_PORT(port, "write-char: Can only write to textual ports.");

  if (port->data_kind == MeschePortDataKindFile) {
    if (c != fputc(c, port->data.file.fp)) {
      // TODO: REPORT ERROR
    }
  } else {
    string_port_write_char(port, c);
  }
}

Value mesche_port_write_cstring(VM *vm, MeschePort *port, char *string, int count) {
  EXPECT_OPEN_PORT(port);
  EXPECT_TEXT_PORT(port, "write-string: Can only write to textual ports.");
  EXPECT_OUTPUT_PORT(port, "write-string: Can only write to textual ports.");

  if (port->data_kind == MeschePortDataKindFile) {
    if (fwrite(string, count, sizeof(char), port->data.file.fp) != count) {
      // TODO: REPORT ERROR
    }
  } else {
    // Write the string character by character
    for (int i = 0; i < count; i++) {
      string_port_write_char(port, string[i]);
    }
  }
}

#define STRING_BUFFER_SIZE 128

static Value string_port_read_char(MeschePort *port) {
  // Do we have a cached character?
  if (port->data.string.index >= port->data.string.size) {
    return EOF_VAL;
  }

  char c = port->data.string.buffer[port->data.string.index];
  port->data.string.index++;
  return CHAR_VAL(c);
}

static Value file_port_read_char(MeschePort *port) {
  // Read from the file descriptor
  char c = fgetc(port->data.file.fp);
  if (c == EOF) {
    return EOF_VAL;
  }

  return CHAR_VAL(c);
}

Value mesche_port_read_string(VM *vm, MeschePort *port) {
  EXPECT_OPEN_PORT(port);
  EXPECT_TEXT_PORT(port, "read-string: Can only read from textual ports.");
  EXPECT_INPUT_PORT(port, "read-string: Can only read from textual ports.");

  // Choose the read function based on data type
  Value (*read_char)(MeschePort *) =
      port->data_kind == MeschePortDataKindFile ? &file_port_read_char : &string_port_read_char;

  char *buffer = NULL;
  int len = 0, size = 0;
  while (true) {
    // TODO: Take peeked char first
    Value cval = read_char(port); // fgetc(port->data.file.fp);
    if (IS_EOF(cval) || AS_CHAR(cval) == '\n') {
      break;
    }

    // Make sure the buffer is large enough
    if (len == size) {
      size = size > 0 ? size * 2 : STRING_BUFFER_SIZE;
      buffer = realloc(buffer, sizeof(char) * size);
    }

    // Store the character in the buffer
    buffer[len++] = AS_CHAR(cval);
  }

  // Return EOF if no string, otherwise return string object based on buffer
  if (buffer == NULL) {
    return EOF_VAL;
  } else {
    buffer[len] = 0;
    ObjectString *str = mesche_object_make_string(vm, buffer, len);
    free(buffer);
    return OBJECT_VAL(str);
  }
}

/* Value mesche_port_printf(VM *vm, MeschePort *port, char *string, int count) { */
/*   EXPECT_OPEN_PORT(port); */
/*   EXPECT_TEXT_PORT(port, "write-string: Can only write to textual ports."); */

/*   if (port->data_kind == MeschePortDataKindFile) { */
/*     if (fwrite(string, count, sizeof(char), port->data.file.fp) != count) { */
/*       // TODO: REPORT ERROR */
/*     } */
/*   } else { */
/*     // Check buffer size */
/*     if (port->data.string.size - port->data.string.index == 0) { */
/*       port->data.string.buffer = */
/*           realloc(port->data.string.buffer, */
/*                   port->data.string.size ? port->data.string.size * 2 :
 * INITIAL_STRING_PORT_SIZE); */
/*     } */

/*     // TODO: Reallocate buffer if snprintf needs more space */
/*     int chars_to_write = snprintf(port->data.string.buffer + port->data.string.index, "%*s", */
/*                                   count < 0 ? strlen(string) : count, string); */
/*   } */
/* } */

Value mesche_port_write_string(VM *vm, MeschePort *port, ObjectString *string, int start, int end) {
  return mesche_port_write_cstring(vm, port, string->chars + start, end - start);
}

void mesche_io_port_print(MeschePort *output_port, MeschePort *port, MeschePrintStyle style) {
  /* mesche_port_write_cstring(vm, port, "#<", 2); */
  fputs("#<", output_port->data.file.fp);

  if (port->is_closed) {
    fputs("closed: ", output_port->data.file.fp);
  } else {
    switch (port->kind) {
    case MeschePortKindInput:
      fputs("input: ", output_port->data.file.fp);
      break;
    case MeschePortKindOutput:
      fputs("output: ", output_port->data.file.fp);
      break;
    }

    if (port->data_kind == MeschePortDataKindFile) {
      fprintf(output_port->data.file.fp, "file %s",
              port->data.file.name ? port->data.file.name->chars : "(unnamed)");
    } else {
      fprintf(output_port->data.file.fp, "string");
      /* fprintf(output_port->data.file.fp, "string %s", */
      /*         port->data.string.buffer ? port->data.file.name : "(unnamed)"); */
    }
  }

  fputs(">", output_port->data.file.fp);
}

Value mesche_port_output_string(VM *vm, MeschePort *port) {
  EXPECT_OUTPUT_PORT(port, "get-output-string: A string output port is required.");
  EXPECT_STRING_PORT(port, "get-output-string: A string output port is required.");

  return OBJECT_VAL(
      mesche_object_make_string(vm, port->data.string.buffer, port->data.string.index));
}

Value read_all_text_msc(VM *vm, int arg_count, Value *args) {
  MeschePort *port = NULL;
  EXPECT_ARG_COUNT(1);
  EXPECT_OBJECT_KIND(ObjectKindPort, 0, AS_PORT, port);
  EXPECT_TEXT_PORT(port, "read-all-text: Can only read from a textual input port.")
  EXPECT_INPUT_PORT(port, "read-all-text: Can only read from a textual input port.");

  int buffer_size = 1024;
  char *buffer = malloc(sizeof(char) * buffer_size);

  int length = 0;
  int read_count = 0;
  int read_limit = buffer_size - 1;
  while ((read_count = fread(buffer + length, sizeof(char), read_limit, port->data.file.fp))) {
    int prev_length = length;
    length += read_count;
    read_limit = (buffer_size - length - 1);
    if (feof(port->data.file.fp) == 0 && read_limit <= 0) {
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

  return mesche_io_make_file_port_from_path(vm, MeschePortKindInput, file_path->chars,
                                            MeschePortFileFlagsNone);
}

Value open_output_file_msc(VM *vm, int arg_count, Value *args) {
  ObjectString *file_path = NULL;
  EXPECT_ARG_COUNT(1);
  EXPECT_OBJECT_KIND(ObjectKindString, 0, AS_STRING, file_path);

  // TODO: Add keyword arguments for flags

  return mesche_io_make_file_port_from_path(vm, MeschePortKindOutput, file_path->chars,
                                            MeschePortFileFlagsNone);
}

Value open_input_string_msc(VM *vm, int arg_count, Value *args) {
  ObjectString *input_string = NULL;
  EXPECT_ARG_COUNT(1);
  EXPECT_OBJECT_KIND(ObjectKindString, 0, AS_STRING, input_string);

  return mesche_io_make_string_port(vm, MeschePortKindInput, input_string->chars,
                                    input_string->length);
}

Value open_output_string_msc(VM *vm, int arg_count, Value *args) {
  ObjectString *file_path = NULL;
  EXPECT_ARG_COUNT(0);

  return mesche_io_make_string_port(vm, MeschePortKindOutput, "", 0);
}

Value get_output_string_msc(VM *vm, int arg_count, Value *args) {
  MeschePort *port = NULL;
  EXPECT_ARG_COUNT(1);
  EXPECT_OBJECT_KIND(ObjectKindPort, 0, AS_PORT, port);

  return mesche_port_output_string(vm, port);
}

Value close_port_msc(VM *vm, int arg_count, Value *args) {
  MeschePort *port = NULL;
  EXPECT_ARG_COUNT(1);
  EXPECT_OBJECT_KIND(ObjectKindPort, 0, AS_PORT, port);

  return mesche_port_close(vm, port);
}

Value close_input_port_msc(VM *vm, int arg_count, Value *args) {
  MeschePort *port = NULL;
  EXPECT_ARG_COUNT(1);
  EXPECT_OBJECT_KIND(ObjectKindPort, 0, AS_PORT, port);

  if (port->kind != MeschePortKindInput) {
    return mesche_error(vm, "close-input-port: Cannot close output port.");
  }

  return mesche_port_close(vm, port);
}

Value close_output_port_msc(VM *vm, int arg_count, Value *args) {
  MeschePort *port = NULL;
  EXPECT_ARG_COUNT(1);
  EXPECT_OBJECT_KIND(ObjectKindPort, 0, AS_PORT, port);

  if (port->kind != MeschePortKindOutput) {
    return mesche_error(vm, "close-output-port: Cannot close input port.");
  }

  return mesche_port_close(vm, port);
}

Value mesche_port_read_char(VM *vm, MeschePort *port) {
  EXPECT_TEXT_PORT(port, "read-char can only read from textual input ports.")
  EXPECT_INPUT_PORT(port, "read-char can only read from textual input ports.")

  if (port->data_kind == MeschePortDataKindString) {
    return string_port_read_char(port);
  }

  return file_port_read_char(port);
}

Value mesche_port_peek_char(VM *vm, MeschePort *port) {
  EXPECT_TEXT_PORT(port, "read-char can only read from textual input ports.")
  EXPECT_INPUT_PORT(port, "read-char can only read from textual input ports.")

  if (port->data_kind == MeschePortDataKindString) {
    if (port->data.string.index >= port->data.string.size) {
      return EOF_VAL;
    }

    char c = port->data.string.buffer[port->data.string.index];
    port->data.string.index++;
    return CHAR_VAL(c);
  } else {
    if (feof(port->data.file.fp)) {
      return EOF_VAL;
    }

    return CHAR_VAL(fgetc(port->data.file.fp));
  }

  return mesche_port_close(vm, port);
}

Value read_char_msc(VM *vm, int arg_count, Value *args) {
  MeschePort *port = NULL;
  EXPECT_ARG_COUNT(1);
  EXPECT_OBJECT_KIND(ObjectKindPort, 0, AS_PORT, port);

  return mesche_port_read_char(vm, port);
}

Value read_line_msc(VM *vm, int arg_count, Value *args) {
  MeschePort *port = NULL;
  EXPECT_ARG_COUNT(1);
  EXPECT_OBJECT_KIND(ObjectKindPort, 0, AS_PORT, port);

  return mesche_port_read_string(vm, port);
}

Value write_char_msc(VM *vm, int arg_count, Value *args) {
  MeschePort *port = NULL;
  ObjectString *char_string = NULL;
  EXPECT_ARG_COUNT(2);
  EXPECT_OBJECT_KIND(ObjectKindString, 0, AS_STRING, char_string);
  // TODO: Port is not required, use the current port
  EXPECT_OBJECT_KIND(ObjectKindPort, 1, AS_PORT, port);

  return mesche_port_write_char(vm, port, char_string->chars[0]);
}

Value write_string_msc(VM *vm, int arg_count, Value *args) {
  MeschePort *port = NULL;
  ObjectString *char_string = NULL;
  EXPECT_ARG_COUNT(2);
  EXPECT_OBJECT_KIND(ObjectKindString, 0, AS_STRING, char_string);
  EXPECT_OBJECT_KIND(ObjectKindPort, 1, AS_PORT, port);

  return mesche_port_write_string(vm, port, char_string, 0, char_string->length);
}

void mesche_io_module_init(VM *vm) {
  mesche_vm_define_native_funcs(
      vm, "mesche io",
      (MescheNativeFuncDetails[]){{"open-input-file", open_input_file_msc, true},
                                  {"open-output-file", open_output_file_msc, true},
                                  {"open-input-string", open_input_string_msc, true},
                                  {"open-output-string", open_output_string_msc, true},
                                  {"get-output-string", get_output_string_msc, true},
                                  {"close-port", close_port_msc, true},
                                  {"close-input-port", close_input_port_msc, true},
                                  {"close-output-port", close_output_port_msc, true},
                                  {"read-char", read_char_msc, true},
                                  {"write-char", write_char_msc, true},
                                  {"read-line", read_line_msc, true},
                                  {"write-string", write_string_msc, true},
                                  /* {"peek-char", peek_char_msc, true}, */
                                  /* {"read-u8", read_u8_msc, true}, */
                                  /* {"peek-u8", peek_u8_msc, true}, */
                                  {"read-all-text", read_all_text_msc, true},
                                  {NULL, NULL, false}});
}
