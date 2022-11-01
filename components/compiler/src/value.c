#include <stdio.h>
#include <string.h>

#include "mem.h"
#include "object.h"
#include "symbol.h"
#include "value.h"

void mesche_value_array_init(ValueArray *array) {
  array->count = 0;
  array->capacity = 0;
  array->values = NULL;
}

void mesche_value_array_write(MescheMemory *mem, ValueArray *array, Value value) {
  if (array->capacity < array->count + 1) {
    int old_capacity = array->capacity;
    array->capacity = GROW_CAPACITY(old_capacity);
    array->values = GROW_ARRAY(mem, Value, array->values, old_capacity, array->capacity);
  }

  array->values[array->count] = value;
  array->count++;
}

void mesche_value_array_free(MescheMemory *mem, ValueArray *array) {
  FREE_ARRAY(mem, Value, array->values, array->capacity);
  mesche_value_array_init(array);
}

void mesche_value_print(Value value) {
  switch (value.kind) {
  case VALUE_UNSPECIFIED:
    printf("#<unspecified>");
    break;
  case VALUE_NUMBER:
    printf("%g", AS_NUMBER(value));
    break;
  case VALUE_FALSE:
    printf("#f");
    break;
  case VALUE_TRUE:
    printf("#t");
    break;
  case VALUE_EMPTY:
    printf("()");
    break;
  case VALUE_OBJECT:
    mesche_object_print(value);
    break;
  case VALUE_EOF:
    printf("#<eof>");
    break;
  }
}

bool mesche_value_eqv_p(Value a, Value b) {
  // This check also covers comparison of #t and #f
  if (a.kind != b.kind)
    return false;

  switch (a.kind) {
  case VALUE_FALSE:
  case VALUE_TRUE:
  case VALUE_EMPTY:
    return true;
  case VALUE_NUMBER:
    return AS_NUMBER(a) == AS_NUMBER(b);
  case VALUE_OBJECT: {
    // Compare the names of the two symbols since they are interned
    if (IS_SYMBOL(a) && IS_SYMBOL(b)) {
      return AS_SYMBOL(a)->name == AS_SYMBOL(b)->name;
    } else {
      return AS_OBJECT(a) == AS_OBJECT(b);
    }
  }
  default:
    return false;
  }
}
