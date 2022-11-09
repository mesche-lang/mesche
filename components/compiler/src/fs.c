#include <errno.h>
#include <libgen.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "fs.h"
#include "native.h"
#include "object.h"
#include "util.h"

bool mesche_fs_path_exists_p(const char *fs_path) { return access(fs_path, F_OK) != -1; }

bool mesche_fs_path_absolute_p(const char *fs_path) { return fs_path[0] == '/'; }

// NOTE: Caller must free the result string!
char *mesche_fs_resolve_path(const char *fs_path) {
  // Allocate a buffer and make it an empty string
  char path_buf[PATH_MAX];
  path_buf[0] = '\0';

  // If the path itself is not absolute, start from the current directory
  if (fs_path[0] != '/') {
    getcwd(&path_buf[0], sizeof(path_buf));
  }

  // Now we have our starting path, loop through it and find each new part of the path
  // We start after the current path since it must already exist.
  int path_start = strlen(path_buf) + 1;
  path_buf[path_start - 1] = '/';
  path_buf[path_start] = '\0';

  for (int i = 0; i < strlen(fs_path); i++) {
    path_buf[path_start + i] = fs_path[i];
    path_buf[path_start + i + 1] = '\0';

    if (fs_path[i] == '/') {
      char resolve_buf[PATH_MAX];
      if (realpath(path_buf, &resolve_buf) != NULL) {
        // TODO: Use strcpy_s!
        // https://www.cisa.gov/uscert/bsi/articles/knowledge/coding-practices/strcpy_s-and-strcat_s
        strcpy(path_buf, resolve_buf);
        path_start = strlen(path_buf) + 1;
        path_buf[path_start - 1] = '/';
        path_buf[path_start] = '\0';
        path_start -= i + 1; // Adjust for future character placements
      } else {
        // We've reached a part of the path that can't be resolved but
        // we'll keep copying until the end of the string since we don't
        // expect the full path to exist.
      }
    }
  }

  return strdup(path_buf);
}

char *mesche_fs_file_directory(const char *file_path) { return dirname((char *)file_path); }

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

Value fs_path_exists_p_msc(VM *vm, int arg_count, Value *args) {
  return BOOL_VAL(access(AS_CSTRING(args[0]), F_OK) != -1);
}

Value fs_path_resolve_msc(VM *vm, int arg_count, Value *args) {
  char *resolved_path = mesche_fs_resolve_path(AS_CSTRING(args[0]));
  ObjectString *resolved_path_str =
      mesche_object_make_string(vm, resolved_path, strlen(resolved_path));
  free(resolved_path);

  return OBJECT_VAL(resolved_path_str);
}

Value fs_file_extension_msc(VM *vm, int arg_count, Value *args) {
  const char *file_path = AS_CSTRING(args[0]);
  const char *dot = strrchr(file_path, '.');
  const char *ext = dot ? dot + 1 : "";
  ObjectString *ext_str = mesche_object_make_string(vm, ext, strlen(ext));

  return OBJECT_VAL(ext_str);
}

Value fs_file_name_msc(VM *vm, int arg_count, Value *args) {
  ObjectString *file_name_str = AS_STRING(args[0]);
  const char *dot = strrchr(file_name_str->chars, '.');
  if (dot) {
    file_name_str = mesche_object_make_string(vm, file_name_str->chars, dot - file_name_str->chars);
  }

  return OBJECT_VAL(file_name_str);
}

Value fs_file_directory_msc(VM *vm, int arg_count, Value *args) {
  // dirname may change the original string, so duplicate it first
  char *file_path = strdup(AS_CSTRING(args[0]));
  char *dir_name = dirname(file_path);
  ObjectString *dir_name_str = mesche_object_make_string(vm, dir_name, strlen(dir_name));

  // NOTE: dir_name doesn't need to be freed because it doesn't cause an allocation
  free(file_path);

  return OBJECT_VAL(dir_name_str);
}

Value fs_file_modified_time_msc(VM *vm, int arg_count, Value *args) {
  const char *file_path = AS_CSTRING(args[0]);
  struct stat file_stat;

  if (stat(file_path, &file_stat) != 0) {
    return NUMBER_VAL(0);
  }

  return NUMBER_VAL(file_stat.st_mtime);
}

Value fs_file_read_all_msc(VM *vm, int arg_count, Value *args) {
  char *file_contents = mesche_fs_file_read_all(AS_CSTRING(args[0]));
  ObjectString *file_contents_str =
      mesche_object_make_string(vm, file_contents, strlen(file_contents));

  // Free the string that we read from the file
  free(file_contents);

  return OBJECT_VAL(file_contents_str);
}

int fs_directory_create(const char *dir_path) {
  // TODO: Make permissions a parameter
  return mkdir(dir_path, 0777);
}

Value fs_directory_create_msc(VM *vm, int arg_count, Value *args) {
  // TODO: Write out error on failure
  int result = fs_directory_create(AS_CSTRING(args[0]));

  return result == 0 ? TRUE_VAL : FALSE_VAL;
}

Value fs_path_ensure_msc(VM *vm, int arg_count, Value *args) {
  // Ensure we're looking at a string
  if (!IS_STRING(args[0])) {
    // TODO: Throw an error
    PANIC("Received a non-string input: %d\n", AS_OBJECT(args[0])->kind);
  }

  char *resolved_path = mesche_fs_resolve_path(AS_CSTRING(args[0]));

  // Step through the path making sure each part exists
  int path_len = strlen(resolved_path);
  for (int i = 0; i < path_len; i++) {
    if (resolved_path[i] == '/' || i + 1 == path_len) {
      // Temporarily terminate the string at this location
      char tmp = resolved_path[i + 1];
      resolved_path[i + 1] = '\0';

      // Create the directory if it doesn't already exist
      if (!mesche_fs_path_exists_p(resolved_path)) {
        if (fs_directory_create(resolved_path) == -1) {
          // TODO: Throw a language error instead
          if (errno == EEXIST) {
            printf(
                "A file with the same name already exists where a directory is being created: %s\n",
                errno, resolved_path);
            return FALSE_VAL;
          } else {
            printf("Error %d while creating path: %s\n", errno, resolved_path);
            return FALSE_VAL;
          }
        }
      }

      // Restore the previous character
      resolved_path[i + 1] = tmp;
    }
  }

  // Create a resolved string to be returned
  ObjectString *resolved_path_str =
      mesche_object_make_string(vm, resolved_path, strlen(resolved_path));
  free(resolved_path);

  return OBJECT_VAL(resolved_path_str);
}

void mesche_fs_module_init(VM *vm) {
  mesche_vm_define_native_funcs(
      vm, "mesche fs",
      (MescheNativeFuncDetails[]){{"path-exists?", fs_path_exists_p_msc, true},
                                  {"path-resolve", fs_path_resolve_msc, true},
                                  {"path-ensure", fs_path_ensure_msc, true},
                                  {"file-name", fs_file_name_msc, true},
                                  {"file-directory", fs_file_directory_msc, true},
                                  {"file-extension", fs_file_extension_msc, true},
                                  {"file-modified-time", fs_file_modified_time_msc, true},
                                  {"file-read-all", fs_file_read_all_msc, true},
                                  {"directory-create", fs_directory_create_msc, true},
                                  {NULL, NULL, false}});
}
