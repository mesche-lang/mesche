#include <stdio.h>

#include "array.h"
#include "closure.h"
#include "continuation.h"
#include "error.h"
#include "function.h"
#include "io.h"
#include "keyword.h"
#include "mem.h"
#include "native.h"
#include "object.h"
#include "record.h"
#include "string.h"
#include "symbol.h"
#include "syntax.h"
#include "util.h"
#include "vm-impl.h"

Object *mesche_object_allocate(VM *vm, size_t size, ObjectKind kind) {
  Object *object = (Object *)mesche_mem_realloc((MescheMemory *)vm, NULL, 0, size);
  object->kind = kind;

  // Keep track of the object for garbage collection
  object->is_marked = false;
  object->next = vm->objects;
  vm->objects = object;

#ifdef DEBUG_LOG_GC
  printf("%p    allocate %zu for %d\n", (void *)object, size, kind);
#endif

  return object;
}

ObjectCons *mesche_object_make_cons(VM *vm, Value car, Value cdr) {
  ObjectCons *cons = ALLOC_OBJECT(vm, ObjectCons, ObjectKindCons);
  cons->car = car;
  cons->cdr = cdr;

  return cons;
}

ObjectStackMarker *mesche_object_make_stack_marker(VM *vm, StackMarkerKind kind,
                                                   uint8_t frame_index) {
  ObjectStackMarker *marker = ALLOC_OBJECT(vm, ObjectStackMarker, ObjectKindStackMarker);
  marker->kind = kind;
  marker->frame_index = frame_index;

  return marker;
}

void mesche_object_free(VM *vm, Object *object) {
#ifdef DEBUG_LOG_GC
  printf("%p    free   ", (void *)object);
  mesche_value_print(port, OBJECT_VAL(object));
  printf(" - kind: %d", object->kind);
  printf("\n");
#endif

  switch (object->kind) {
  case ObjectKindString:
    mesche_free_string(vm, (ObjectString *)object);
    break;
  case ObjectKindSymbol:
    mesche_free_symbol(vm, (ObjectSymbol *)object);
    break;
  case ObjectKindKeyword:
    mesche_free_keyword(vm, (ObjectKeyword *)object);
    break;
  case ObjectKindSyntax:
    mesche_free_syntax(vm, (ObjectSyntax *)object);
    break;
  case ObjectKindCons:
    FREE(vm, ObjectCons, object);
    break;
  case ObjectKindArray:
    mesche_free_array(vm, (ObjectArray *)object);
    break;
  case ObjectKindUpvalue:
    mesche_free_upvalue(vm, (ObjectUpvalue *)object);
    break;
  case ObjectKindFunction:
    mesche_free_function(vm, (ObjectFunction *)object);
    break;
  case ObjectKindClosure:
    mesche_free_closure(vm, (ObjectClosure *)object);
    break;
  case ObjectKindContinuation:
    mesche_free_continuation(vm, (ObjectContinuation *)object);
    break;
  case ObjectKindStackMarker: {
    ObjectStackMarker *marker = (ObjectStackMarker *)object;
    FREE(vm, ObjectStackMarker, object);
    break;
  }
  case ObjectKindNativeFunction:
    mesche_free_native_function(vm, (ObjectNativeFunction *)object);
    break;
  case ObjectKindPointer:
    mesche_free_pointer(vm, (ObjectPointer *)object);
    break;
  case ObjectKindModule:
    mesche_free_module(vm, (ObjectModule *)object);
    break;
  case ObjectKindPort:
    mesche_free_port(vm, (MeschePort *)object);
    break;
  case ObjectKindRecord: {
    mesche_free_record(vm, (ObjectRecord *)object);
    break;
  }
  case ObjectKindRecordInstance: {
    ObjectRecordInstance *instance = (ObjectRecordInstance *)object;
    mesche_value_array_free((MescheMemory *)vm, &instance->field_values);
    FREE(vm, ObjectRecordInstance, object);
    break;
  }
  case ObjectKindRecordField:
    FREE(vm, ObjectRecordField, object);
    break;
  case ObjectKindRecordFieldAccessor:
    FREE(vm, ObjectRecordFieldAccessor, object);
    break;
  case ObjectKindRecordFieldSetter:
    FREE(vm, ObjectRecordFieldSetter, object);
    break;
  case ObjectKindError:
    mesche_free_error(vm, (MescheError *)object);
    break;
  default:
    PANIC("Don't know how to free object kind %d!", object->kind);
  }
}

static void print_function(MeschePort *port, ObjectFunction *function) {
  if (function->name == NULL) {
    if (function->type == TYPE_SCRIPT) {
      printf("<script 0x%p>", function);
    } else {
      printf("<lambda 0x%p>", function);
    }
    return;
  }

  printf("<fn %s 0x%p>", function->name->chars, function);
}

void mesche_object_print(MeschePort *port, Value value) {
  mesche_object_print_ex(port, value, PrintStyleOutput);
}

void mesche_object_print_ex(MeschePort *port, Value value, MeschePrintStyle style) {
  switch (OBJECT_KIND(value)) {
  case ObjectKindString:
    fprintf(port->fp, "%s", AS_CSTRING(value));
    break;
  case ObjectKindSymbol:
    fprintf(port->fp, "%s", AS_SYMBOL(value)->name->chars);
    break;
  case ObjectKindKeyword:
    fprintf(port->fp, ":%s", AS_CSTRING(value));
    break;
  case ObjectKindSyntax:
    mesche_syntax_print_ex(port, AS_SYNTAX(value), style);
    break;
  case ObjectKindCons: {
    ObjectCons *cons = AS_CONS(value);
    fputs("(", port->fp);

    for (;;) {
      mesche_value_print_ex(port, cons->car, style);
      if (IS_EMPTY(cons->cdr)) {
        break;
      } else if (IS_CONS(cons->cdr)) {
        cons = AS_CONS(cons->cdr);
      } else {
        fputs(" . ", port->fp);
        mesche_value_print(port, cons->cdr);
        break;
      }

      fputs(" ", port->fp);
    }

    fputs(")", port->fp);
    break;
  }
  case ObjectKindArray:
    fprintf(port->fp, "#<array %p>", AS_OBJECT(value));
    break;
  case ObjectKindUpvalue:
    fputs("upvalue", port->fp);
    break;
  case ObjectKindFunction:
    print_function(port, AS_FUNCTION(value));
    break;
  case ObjectKindClosure:
    print_function(port, AS_CLOSURE(value)->function);
    break;
  case ObjectKindContinuation: {
    ObjectContinuation *continuation = AS_CONTINUATION(value);
    fprintf(port->fp, "#<continuation frames: %d, values: %d>", continuation->frame_count,
            continuation->stack_count);
    break;
  }
  case ObjectKindStackMarker: {
    ObjectStackMarker *marker = AS_STACK_MARKER(value);
    fprintf(port->fp, "#<stack marker: %s @ %d %p>",
            marker->kind == STACK_MARKER_RESET ? "reset" : "shift", marker->frame_index,
            AS_OBJECT(value));
    break;
  }
  case ObjectKindNativeFunction:
    fprintf(port->fp, "#<native fn>");
    break;
  case ObjectKindPointer: {
    ObjectPointer *pointer = AS_POINTER(value);
    if (pointer->type != NULL) {
      // TODO: Add custom print function
      fprintf(port->fp, "#<%s %p>", pointer->type->name, AS_POINTER(value)->ptr);
    } else {
      fprintf(port->fp, "<pointer %p>", AS_POINTER(value)->ptr);
    }
    break;
  }
  case ObjectKindModule: {
    ObjectModule *module = (ObjectModule *)AS_OBJECT(value);
    fprintf(port->fp, "#<module (%s) %p>", module->name->chars, module);
    break;
  }
  case ObjectKindPort:
    mesche_io_port_print(port, AS_PORT(value), style);
    break;
  case ObjectKindRecord: {
    ObjectRecord *record = (ObjectRecord *)AS_OBJECT(value);
    fprintf(port->fp, "#<record type '%s' %p>", record->name->chars, record);
    break;
  }
  case ObjectKindRecordFieldAccessor: {
    ObjectRecordFieldAccessor *accessor = (ObjectRecordFieldAccessor *)AS_OBJECT(value);
    ObjectRecordField *field =
        AS_RECORD_FIELD(accessor->record_type->fields.values[accessor->field_index]);
    fprintf(port->fp, "#<record field getter '%s' %p>", field->name->chars, accessor);
    break;
  }
  case ObjectKindRecordFieldSetter: {
    ObjectRecordFieldSetter *setter = (ObjectRecordFieldSetter *)AS_OBJECT(value);
    ObjectRecordField *field =
        AS_RECORD_FIELD(setter->record_type->fields.values[setter->field_index]);
    fprintf(port->fp, "#<record field setter '%s' %p>", field->name->chars, setter);
    break;
  }
  case ObjectKindRecordInstance: {
    ObjectRecordInstance *record = (ObjectRecordInstance *)AS_OBJECT(value);
    fprintf(port->fp, "#<record '%s' %p>", record->record_type->name->chars, record);
    break;
  }
  default:
    fprintf(port->fp, "#<unknown>");
    break;
  }
}

inline bool mesche_object_is_kind(Value value, ObjectKind kind) {
  return IS_OBJECT(value) && AS_OBJECT(value)->kind == kind;
}

bool mesche_object_string_equalsp(Object *left, Object *right) {
  if (!(left->kind == ObjectKindString || left->kind == ObjectKindKeyword) &&
      !(right->kind == ObjectKindString || right->kind == ObjectKindKeyword)) {
    return false;
  }

  ObjectString *left_str = (ObjectString *)left;
  ObjectString *right_str = (ObjectString *)right;

  return (left_str->length == right_str->length && left_str->hash == right_str->hash &&
          memcmp(left_str->chars, right_str->chars, left_str->length) == 0);
}
