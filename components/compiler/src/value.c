#include <stdio.h>
#include <string.h>

#include "io.h"
#include "mem.h"
#include "object.h"
#include "port.h"
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

void mesche_value_print(MeschePort *port, Value value) {
  mesche_value_print_ex(port, value, PrintStyleOutput);
}

void mesche_value_print_ex(MeschePort *port, Value value, MeschePrintStyle style) {
  switch (value.kind) {
  case VALUE_UNSPECIFIED:
    if (style == PrintStyleData) {
      fprintf(port->data.file.fp, "#<unspecified>");
    }
    break;
  case VALUE_NUMBER:
    fprintf(port->data.file.fp, "%g", AS_NUMBER(value));
    break;
  case VALUE_CHAR:
    fprintf(port->data.file.fp, "%c", AS_CHAR(value));
    break;
  case VALUE_FALSE:
    fprintf(port->data.file.fp, "#f");
    break;
  case VALUE_TRUE:
    fprintf(port->data.file.fp, "#t");
    break;
  case VALUE_EMPTY:
    if (style == PrintStyleOutput) {
      fprintf(port->data.file.fp, "()");
    } else {
      fprintf(port->data.file.fp, "'()");
    }
    break;
  case VALUE_OBJECT:
    mesche_object_print_ex(port, value, style);
    break;
  case VALUE_EOF:
    fprintf(port->data.file.fp, "#<eof>");
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
  case VALUE_CHAR:
    return AS_CHAR(a) == AS_CHAR(b);
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
