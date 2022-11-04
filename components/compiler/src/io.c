#include <stdlib.h>
#include <string.h>

#include "io.h"
#include "native.h"
#include "object.h"
#include "value.h"
#include "vm.h"

char *mesche_io_read_all_text(MeschePort *port) {
  int size = 256;
  char *buffer = malloc(sizeof(char) * (size + 1));
  FILE *fp = (FILE *)port;

  int length = 0;
  int read_count = 0;
  while (read_count = fread(buffer, sizeof(char), size - read_count, fp)) {
    length += read_count;
    if (feof(fp) != 0 && length >= size) {
      // Increase the size of the buffer
      size = size + 1024;
      buffer = realloc(buffer, size + 1);
    }
  }

  // Null-terminate the buffer before returning it
  buffer[length] = 0;
  return buffer;
}

Value read_all_text_msc(MescheMemory *mem, int arg_count, Value *args) {
  // First parameter must be a MeschePort wrapped in an ObjectPointer
  ObjectPointer *pointer = AS_POINTER(args[0]);

  char *contents = mesche_io_read_all_text((MeschePort *)pointer->ptr);
  ObjectString *contents_string = mesche_object_make_string((VM *)mem, contents, strlen(contents));
  free(contents);

  return OBJECT_VAL(contents_string);
}

void mesche_io_module_init(VM *vm) {
  mesche_vm_define_native_funcs(
      vm, "mesche io",
      (MescheNativeFuncDetails[]){{"read-all-text", read_all_text_msc, true}, {NULL, NULL, false}});
}
