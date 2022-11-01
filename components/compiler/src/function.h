#ifndef mesche_function_h
#define mesche_function_h

#include "chunk.h"
#include "mem.h"
#include "string.h"
#include "vm.h"

typedef enum { TYPE_FUNCTION, TYPE_SCRIPT } FunctionType;

typedef struct {
  ObjectString *name;
  uint8_t default_index;
} KeywordArgument;

typedef struct {
  int capacity;
  int count;
  KeywordArgument *args;
} KeywordArgumentArray;

typedef struct ObjectFunction {
  Object object;
  FunctionType type;
  int arity;
  int rest_arg_index;
  int upvalue_count;
  Chunk chunk;
  KeywordArgumentArray keyword_args;
  ObjectString *name;
} ObjectFunction;

#define IS_FUNCTION(value) mesche_object_is_kind(value, ObjectKindFunction)
#define AS_FUNCTION(value) ((ObjectFunction *)AS_OBJECT(value))

ObjectFunction *mesche_object_make_function(VM *vm, FunctionType type);
void mesche_free_function(VM *vm, ObjectFunction *function);

void mesche_object_function_keyword_add(MescheMemory *mem, ObjectFunction *function,
                                        KeywordArgument keyword_arg);

#endif
