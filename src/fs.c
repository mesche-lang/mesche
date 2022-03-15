#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <libgen.h>
#include <unistd.h>

#include "fs.h"
#include "object.h"
#include "util.h"

bool mesche_fs_path_exists_p(const char *fs_path) { return access(fs_path, F_OK) != -1; }

bool mesche_fs_path_absolute_p(const char *fs_path) { return fs_path[0] == '/'; }

// NOTE: Caller must free the result string!
char *mesche_fs_resolve_path(const char *fs_path) {
  if (mesche_fs_path_absolute_p(fs_path)) {
    // Return a copy of the string that can be freed so that the caller can deal
    // with it consistently
    return strdup(fs_path);
  }

  char *current_path = getcwd(NULL, 0);
  if (current_path != NULL) {
    size_t size = strlen(current_path) + strlen(fs_path) + 2;
    char *joined_path = malloc(size);
    snprintf(joined_path, size, "%s/%s", current_path, fs_path);

    // Resolve the joined path and free the intermediate strings
    char *resolved_path = realpath(joined_path, NULL);
    free(current_path);
    free(joined_path);
    return resolved_path;
  } else {
    PANIC("Error while calling getcwd()");
  }
}

char *mesche_fs_file_directory(const char *file_path) {
  return dirname(file_path);
}

char *mesche_fs_file_read_all(const char *file_path) {
  FILE *file = fopen(file_path, "rb");

  // TODO: Use a less catastrophic error report
  if (file == NULL) {
    PANIC("Could not open file \"%s\"!\n", file_path);
  }

  // Seek to the end to figure out how big of a char buffer we need
  fseek(file, 0L, SEEK_END);
  size_t file_size = ftell(file);
  rewind(file);

  // Allocate the buffer and read the file contents into it
  char *buffer = (char *)malloc(file_size + 1);
  if (buffer == NULL) {
    PANIC("Could not allocate enough memory to read file \"%s\"!\n", file_path);
  }

  size_t bytes_read = fread(buffer, sizeof(char), file_size, file);
  if (bytes_read < file_size) {
    PANIC("Could not read contents of file \"%s\"!\n", file_path);
  }

  // Finish off the buffer with a null terminator
  buffer[bytes_read] = '\0';
  fclose(file);

  return buffer;
}

Value fs_path_exists_p_msc(MescheMemory *mem, int arg_count, Value *args) {
  return BOOL_VAL(access(AS_CSTRING(args[0]), F_OK) != -1);
}

Value fs_path_resolve_msc(MescheMemory *mem, int arg_count, Value *args) {
  char *resolved_path = mesche_fs_resolve_path(AS_CSTRING(args[0]));
  ObjectString *resolved_path_str = mesche_object_make_string(mem, resolved_path, strlen(resolved_path));
  free(resolved_path);

  return OBJECT_VAL(resolved_path_str);
}

Value fs_file_extension_msc(MescheMemory *mem, int arg_count, Value *args) {
  const char *file_path = AS_CSTRING(args[0]);
  const char *dot = strrchr(file_path, '.');
  const char *ext = dot ? dot + 1 : "";
  ObjectString *ext_str = mesche_object_make_string((VM *)mem, ext, strlen(ext));

  return OBJECT_VAL(ext_str);
}

Value fs_file_name_msc(MescheMemory *mem, int arg_count, Value *args) {
  ObjectString *file_name_str = AS_STRING(args[0]);
  const char *dot = strrchr(file_name_str->chars, '.');
  if (dot) {
    file_name_str =
        mesche_object_make_string((VM *)mem, file_name_str->chars, dot - file_name_str->chars);
  }

  return OBJECT_VAL(file_name_str);
}

Value fs_file_directory_msc(MescheMemory *mem, int arg_count, Value *args) {
  // dirname may change the original string, so duplicate it first
  char *file_path = strdup(AS_CSTRING(args[0]));
  char *dir_name = dirname(file_path);
  ObjectString *dir_name_str = mesche_object_make_string(mem, dir_name, strlen(dir_name));

  // NOTE: dir_name doesn't need to be freed because it doesn't cause an allocation
  free(file_path);

  return OBJECT_VAL(dir_name_str);
}

Value fs_file_modified_time_msc(MescheMemory *mem, int arg_count, Value *args) {
  const char *file_path = AS_CSTRING(args[0]);
  struct stat file_stat;

  if (stat(file_path, &file_stat) != 0) {
    return NUMBER_VAL(0);
  }

  return NUMBER_VAL(file_stat.st_mtime);
}

void mesche_fs_module_init(VM *vm) {
  mesche_vm_define_native_funcs(
      vm, "mesche fs",
      &(MescheNativeFuncDetails[]){{"path-exists?", fs_path_exists_p_msc, true},
                                   {"path-resolve", fs_path_resolve_msc, true},
                                   {"file-name", fs_file_name_msc, true},
                                   {"file-directory", fs_file_directory_msc, true},
                                   {"file-extension", fs_file_extension_msc, true},
                                   {"file-modified-time", fs_file_modified_time_msc, true},
                                   {NULL, NULL, false}});
}
