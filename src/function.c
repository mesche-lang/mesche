#include "function.h"

static void function_keyword_args_init(KeywordArgumentArray *array) {
  array->count = 0;
  array->capacity = 0;
  array->args = NULL;
}

static void function_keyword_args_free(MescheMemory *mem, KeywordArgumentArray *array) {
  FREE_ARRAY(mem, Value, array->args, array->capacity);
  function_keyword_args_init(array);
}

void mesche_object_function_keyword_add(MescheMemory *mem, ObjectFunction *function,
                                        KeywordArgument keyword_arg) {
  KeywordArgumentArray *array = &function->keyword_args;
  if (array->capacity < array->count + 1) {
    int old_capacity = array->capacity;
    array->capacity = GROW_CAPACITY(old_capacity);
    array->args = GROW_ARRAY(mem, KeywordArgument, array->args, old_capacity, array->capacity);
  }

  array->args[array->count] = keyword_arg;
  array->count++;
}

ObjectFunction *mesche_object_make_function(VM *vm, FunctionType type) {
  ObjectFunction *function = ALLOC_OBJECT(vm, ObjectFunction, ObjectKindFunction);
  function->arity = 0;
  function->upvalue_count = 0;
  function->rest_arg_index = 0;
  function->type = type;
  function->name = NULL;
  function_keyword_args_init(&function->keyword_args);
  mesche_chunk_init(&function->chunk);

  return function;
}

void mesche_free_function(VM *vm, ObjectFunction *function) {
  mesche_chunk_free((MescheMemory *)vm, &function->chunk);
  function_keyword_args_free((MescheMemory *)vm, &function->keyword_args);
  FREE(vm, ObjectFunction, function);
}
