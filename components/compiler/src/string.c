#include <ctype.h>
#include <stdlib.h>

#include "mem.h"
#include "native.h"
#include "object.h"
#include "string.h"
#include "util.h"
#include "value.h"
#include "vm-impl.h"

ObjectString *mesche_object_make_string(VM *vm, const char *chars, int length) {
  // Allocate and initialize the string object
  ObjectString *string = ALLOC_OBJECT_EX(vm, ObjectString, length + 1, ObjectKindString);

  // Copy each letter one at a time to convert escape sequences
  int offset = 0;
  bool escaped = false;
  for (int i = 0; i < length; i++) {
    if (escaped) {
      // Assume we'll find a valid escaped character and increase the offset
      // with which we will copy the remaining characters
      escaped = false;
      offset++;

      // TODO: Error on unexpected escape sequence?
      switch (chars[i]) {
      case '\\':
        string->chars[i - offset] = '\\';
        continue;
      case 'n':
        string->chars[i - offset] = '\n';
        continue;
      case 'e':
        string->chars[i - offset] = '\e';
        continue;
      case 't':
        string->chars[i - offset] = '\t';
        continue;
      }
    } else {
      if (chars[i] == '\\') {
        escaped = true;
        continue;
      }
    }

    // If we've reached this point, just copy the character
    string->chars[i - offset] = chars[i];
  }

  // Compute the final length and finish the string
  length = length - offset;
  string->chars[length] = '\0';

  // Finish initializing the string
  // TODO: Resize the string buffer in memory to reduce waste
  uint32_t hash = mesche_string_hash(string->chars, length);
  string->length = length;
  string->hash = hash;

  // Is the string already interned?
  ObjectString *interned_string = mesche_table_find_key(&vm->strings, chars, length, hash);
  if (interned_string != NULL) {
    // Return the interned string and don't explicitly free the one we allocated
    // since it will get cleaned up by the GC
    return interned_string;
  }

  // Push the string onto the stack temporarily to avoid GC
  mesche_vm_stack_push(vm, OBJECT_VAL(string));

  // Add the string to the interned set
  // TODO: Use an API for this!
  mesche_table_set((MescheMemory *)vm, &vm->strings, string, FALSE_VAL);

  // Pop the string back off the stack
  mesche_vm_stack_pop(vm);

  return string;
}

void mesche_free_string(VM *vm, ObjectString *string) {
  FREE_SIZE(vm, string, (sizeof(ObjectString) + string->length + 1));
}

uint32_t mesche_string_hash(const char *key, int length) {
  // Use the FNV-1a hash algorithm
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619;
  }

  return hash;
}

char *mesche_cstring_join(const char *left, size_t left_length, const char *right,
                          size_t right_length, const char *separator) {
  size_t separator_len = 0;
  size_t new_length = left_length + right_length + 1;
  if (separator != NULL) {
    separator_len = strlen(separator);
    new_length += separator_len;
  }

  // Allocate a temporary buffer into which we'll copy the string parts
  char *string_buffer = malloc(sizeof(char) * new_length);
  char *copy_ptr = string_buffer;

  // Copy the requisite string segments into the new buffer
  memcpy(copy_ptr, left, left_length);
  copy_ptr += left_length;
  if (separator) {
    memcpy(copy_ptr, separator, separator_len);
    copy_ptr += separator_len;
  }
  memcpy(copy_ptr, right, right_length);

  // Add the null char
  string_buffer[new_length - 1] = '\0';

  // Caller is responsible for freeing
  return string_buffer;
}

ObjectString *mesche_string_join(VM *vm, ObjectString *left, ObjectString *right,
                                 const char *separator) {
  // Join the two cstrings
  char *joined_string =
      mesche_cstring_join(left->chars, left->length, right->chars, right->length, separator);

  // Allocate the new string, free the temporary buffer, and return
  // TODO: Try to get new length from join function, don't use strlen
  ObjectString *new_string = mesche_object_make_string(vm, joined_string, strlen(joined_string));
  free(joined_string);
  return new_string;
}

Value string_append_msc(VM *vm, int arg_count, Value *args) {
  // Append all string arguments together
  ObjectString *result_string = AS_STRING(args[0]);
  for (int i = 1; i < arg_count; i++) {
    // Skip all #f's
    if (IS_FALSE(args[i]))
      continue;

    mesche_vm_stack_push(vm, OBJECT_VAL(result_string));
    result_string = mesche_string_join(vm, result_string, AS_STRING(args[i]), NULL);
    mesche_vm_stack_pop(vm);
  }

  // TODO: Support specifying a separator string
  return OBJECT_VAL(result_string);
}

static Value string_join_list(VM *vm, ObjectCons *list, const char *separator) {
  Value current = OBJECT_VAL(list);
  ObjectString *result_string = NULL;
  while (IS_CONS(current)) {
    ObjectCons *cons = AS_CONS(current);
    if (IS_STRING(cons->car)) {
      if (result_string == NULL) {
        result_string = AS_STRING(cons->car);
      } else {
        mesche_vm_stack_push(vm, OBJECT_VAL(result_string));
        result_string = mesche_string_join(vm, result_string, AS_STRING(cons->car), separator);
        mesche_vm_stack_pop(vm);
      }
    } else {
      // ERROR?
    }

    current = cons->cdr;
  }

  return OBJECT_VAL(result_string);
}

Value string_join_msc(VM *vm, int arg_count, Value *args) {
  char *separator = NULL;
  if (arg_count > 1) {
    separator = AS_CSTRING(args[1]);
  }

  Value collection = args[0];
  if (IS_CONS(collection)) {
    return string_join_list(vm, AS_CONS(collection), separator);
    /* } else if (IS_ARRAY(collection)) { */
    /*   return string_join_array(mem, AS_ARRAY(collection), separator); */
  } else {
    PANIC("string-join: Unexpected object kind: %d\n", AS_OBJECT(collection)->kind);
  }
}

Value string_substring_msc(VM *vm, int arg_count, Value *args) {
  ObjectString *str = AS_STRING(args[0]);

  // TODO: Verify that end_index is higher than start_index
  int start_index = AS_NUMBER(args[1]);
  int end_index = (arg_count > 2) ? AS_NUMBER(args[2]) : strlen(AS_CSTRING(args[0]));

  return OBJECT_VAL(
      mesche_object_make_string(vm, &str->chars[start_index], end_index - start_index));
}

Value string_length_msc(VM *vm, int arg_count, Value *args) {
  ObjectString *str = AS_STRING(args[0]);
  return NUMBER_VAL(strlen(str->chars));
}

Value string_trim_msc(VM *vm, int arg_count, Value *args) {
  ObjectString *str = AS_STRING(args[0]);

  int start = 0, end = 0, len = strlen(str->chars);
  for (int i = 0; i < len; i++) {
    start = i;
    if (!isspace(str->chars[i])) {
      break;
    }
  }

  for (int i = len - 1; i >= 0; i--) {
    end = i;
    if (!isspace(str->chars[i])) {
      break;
    }
  }

  // TODO: Checks
  // - start and end are the same index
  // - if start is greater than end?

  return OBJECT_VAL(mesche_object_make_string(vm, &str->chars[start], (end - start) + 1));
}

Value string_equal_msc(VM *vm, int arg_count, Value *args) {
  ObjectString *str1 = AS_STRING(args[0]);
  ObjectString *str2 = AS_STRING(args[1]);
  return BOOL_VAL(strcmp(str1->chars, str2->chars) == 0);
}

Value string_number_to_string_msc(VM *vm, int arg_count, Value *args) {
  char buffer[256];
  int decimal_places = arg_count > 1 ? (int)AS_NUMBER(args[1]) : 0;
  sprintf(buffer, "%.*f", decimal_places, AS_NUMBER(args[0]));
  return OBJECT_VAL(mesche_object_make_string(vm, buffer, strlen(buffer)));
}

Value string_string_to_number_msc(VM *vm, int arg_count, Value *args) {
  ObjectString *str = AS_STRING(args[0]);
  return NUMBER_VAL(atof(str->chars));
}

void mesche_string_module_init(VM *vm) {
  mesche_vm_define_native_funcs(
      vm, "mesche string",
      (MescheNativeFuncDetails[]){{"string-append", string_append_msc, true},
                                  {"string-join", string_join_msc, true},
                                  {"string-length", string_length_msc, true},
                                  {"string-trim", string_trim_msc, true},
                                  {"string=?", string_equal_msc, true},
                                  {"substring", string_substring_msc, true},
                                  {"string->number", string_string_to_number_msc, true},
                                  {"number->string", string_number_to_string_msc, true},
                                  {NULL, NULL, false}});
}
