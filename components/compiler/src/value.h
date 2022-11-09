#ifndef mesche_value_h
#define mesche_value_h

// Typedef some object types here to deal with circular dependency
// issues between vm.h, value.h, and object.h.
typedef struct Object Object;

#include <stdbool.h>

#include "io.h"
#include "mem.h"

#define UNSPECIFIED_VAL ((Value){VALUE_UNSPECIFIED, {.number = 0}})
#define TRUE_VAL ((Value){VALUE_TRUE, {.number = 0}})
#define FALSE_VAL ((Value){VALUE_FALSE, {.number = 0}})
#define EMPTY_VAL ((Value){VALUE_EMPTY, {.number = 0}})
#define EOF_VAL ((Value){VALUE_EOF, {.number = 0}})
#define NUMBER_VAL(value) ((Value){VALUE_NUMBER, {.number = value}})
#define CHAR_VAL(value) ((Value){VALUE_CHAR, {.character = value}})
#define BOOL_VAL(value) ((Value){value ? VALUE_TRUE : VALUE_FALSE, {.number = 0}})

#define AS_NUMBER(value) ((value).as.number)
#define AS_CHAR(value) ((value).as.character)
#define AS_BOOL(value) ((value).kind != VALUE_FALSE)

#define IS_ANY(value) (true)
#define IS_UNSPECIFIED(value) ((value).kind == VALUE_UNSPECIFIED)
#define IS_TRUE(value) ((value).kind == VALUE_TRUE)
#define IS_FALSE(value) ((value).kind == VALUE_FALSE)
#define IS_EMPTY(value) ((value).kind == VALUE_EMPTY)
#define IS_FALSEY(value) (IS_FALSE(value))
#define IS_NUMBER(value) ((value).kind == VALUE_NUMBER)
#define IS_CHAR(value) ((value).kind == VALUE_CHAR)
#define IS_EOF(value) ((value).kind == VALUE_EOF)

#define PRINT_VALUE(vm, label, value)                                                              \
  printf(label);                                                                                   \
  mesche_value_print(vm->output_port, value);                                                      \
  printf("\n");

typedef enum {
  VALUE_UNSPECIFIED,
  VALUE_FALSE,
  VALUE_TRUE,
  VALUE_EMPTY,
  VALUE_NUMBER,
  VALUE_CHAR,
  VALUE_OBJECT,
  VALUE_EOF,
} ValueKind;

typedef struct {
  ValueKind kind;
  union {
    char character;
    double number;
    Object *object;
  } as;
} Value;

typedef struct {
  int capacity;
  int count;
  Value *values;
} ValueArray;

void mesche_value_array_init(ValueArray *array);
void mesche_value_array_write(MescheMemory *mem, ValueArray *array, Value value);
void mesche_value_array_free(MescheMemory *mem, ValueArray *array);
void mesche_value_print(MeschePort *port, Value value);
void mesche_value_print_ex(MeschePort *port, Value value, MeschePrintStyle style);
bool mesche_value_eqv_p(Value a, Value b);

#endif
