#include <stdlib.h>

#include "string.h"
#include "util.h"

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

Value string_append_msc(MescheMemory *mem, int arg_count, Value *args) {
  // Append all string arguments together
  ObjectString *result_string = AS_STRING(args[0]);
  for (int i = 1; i < arg_count; i++) {
    // Skip all nils
    if (IS_NIL(args[i]))
      continue;

    mesche_vm_stack_push((VM *)mem, OBJECT_VAL(result_string));
    result_string = mesche_string_join((VM *)mem, result_string, AS_STRING(args[i]), NULL);
    mesche_vm_stack_pop((VM *)mem);
  }

  // TODO: Support specifying a separator string
  return OBJECT_VAL(result_string);
}

static Value string_join_list(MescheMemory *mem, ObjectCons *list, const char *separator) {
  Value current = OBJECT_VAL(list);
  ObjectString *result_string = NULL;
  while (IS_CONS(current)) {
    ObjectCons *cons = AS_CONS(current);
    if (IS_STRING(cons->car)) {
      if (result_string == NULL) {
        result_string = AS_STRING(cons->car);
      } else {
        mesche_vm_stack_push((VM *)mem, OBJECT_VAL(result_string));
        result_string =
            mesche_string_join((VM *)mem, result_string, AS_STRING(cons->car), separator);
        mesche_vm_stack_pop((VM *)mem);
      }
    } else {
      // ERROR?
    }

    current = cons->cdr;
  }

  return OBJECT_VAL(result_string);
}

Value string_join_msc(MescheMemory *mem, int arg_count, Value *args) {
  char *separator = NULL;
  if (arg_count > 1) {
    separator = AS_CSTRING(args[1]);
  }

  Value collection = args[0];
  if (IS_CONS(collection)) {
    return string_join_list(mem, AS_CONS(collection), separator);
    /* } else if (IS_ARRAY(collection)) { */
    /*   return string_join_array(mem, AS_ARRAY(collection), separator); */
  } else {
    PANIC("string-join: Unexpected object kind: %d\n", AS_OBJECT(collection)->kind);
  }
}

Value string_number_to_string_msc(MescheMemory *mem, int arg_count, Value *args) {
  char buffer[256];
  int decimal_places = arg_count > 1 ? (int)AS_NUMBER(args[1]) : 0;
  sprintf(buffer, "%.*f", decimal_places, AS_NUMBER(args[0]));
  return OBJECT_VAL(mesche_object_make_string((VM *)mem, buffer, strlen(buffer)));
}

void mesche_string_module_init(VM *vm) {
  mesche_vm_define_native_funcs(
      vm, "mesche string",
      (MescheNativeFuncDetails[]){{"string-append", string_append_msc, true},
                                  {"string-join", string_join_msc, true},
                                  {"number->string", string_number_to_string_msc, true},
                                  {NULL, NULL, false}});
}
