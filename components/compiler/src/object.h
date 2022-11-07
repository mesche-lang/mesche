#ifndef mesche_object_h
#define mesche_object_h

#include <stdint.h>

#include "io.h"
#include "value.h"
#include "vm.h"

#define IS_OBJECT(value) ((value).kind == VALUE_OBJECT)
#define AS_OBJECT(value) ((value).as.object)

#define OBJECT_VAL(value) ((Value){VALUE_OBJECT, {.object = (Object *)value}})
#define OBJECT_KIND(value) (AS_OBJECT(value)->kind)

#define IS_CONS(value) mesche_object_is_kind(value, ObjectKindCons)
#define AS_CONS(value) ((ObjectCons *)AS_OBJECT(value))

#define AS_STACK_MARKER(value) ((ObjectStackMarker *)AS_OBJECT(value))
#define IS_RESET_MARKER(value)                                                                     \
  mesche_object_is_kind(value, ObjectKindStackMarker) &&                                           \
      (AS_STACK_MARKER(value)->kind == STACK_MARKER_RESET)
#define IS_SHIFT_MARKER(value)                                                                     \
  mesche_object_is_kind(value, ObjectKindStackMarker) &&                                           \
      (AS_STACK_MARKER(value)->kind == STACK_MARKER_SHIFT)

typedef enum {
  ObjectKindString,
  ObjectKindSymbol,
  ObjectKindKeyword,
  ObjectKindSyntax,
  ObjectKindCons,
  ObjectKindArray,
  ObjectKindUpvalue,
  ObjectKindFunction,
  ObjectKindClosure,
  ObjectKindContinuation,
  ObjectKindStackMarker,
  ObjectKindNativeFunction,
  ObjectKindPointer,
  ObjectKindModule,
  ObjectKindPort,
  ObjectKindRecord,
  ObjectKindRecordInstance,
  ObjectKindRecordField,
  ObjectKindRecordFieldAccessor,
  ObjectKindRecordFieldSetter
} ObjectKind;

struct Object {
  ObjectKind kind;
  bool is_marked;
  struct Object *next;
};

typedef struct ObjectCons {
  struct Object object;
  Value car;
  Value cdr;
} ObjectCons;

typedef enum { STACK_MARKER_RESET, STACK_MARKER_SHIFT } StackMarkerKind;

typedef struct ObjectStackMarker {
  Object object;
  StackMarkerKind kind;
  uint8_t frame_index;
} ObjectStackMarker;

ObjectCons *mesche_object_make_cons(VM *vm, Value car, Value cdr);
ObjectStackMarker *mesche_object_make_stack_marker(VM *vm, StackMarkerKind kind,
                                                   uint8_t frame_index);

void mesche_object_free(VM *vm, struct Object *object);
void mesche_object_print(MeschePort *port, Value value);
void mesche_object_print_ex(MeschePort *port, Value value, MeschePrintStyle style);

bool mesche_object_is_kind(Value value, ObjectKind kind);
bool mesche_object_string_equalsp(Object *left, Object *right);

Object *mesche_object_allocate(VM *vm, size_t size, ObjectKind kind);

#define ALLOC_OBJECT(vm, type, object_kind)                                                        \
  (type *)mesche_object_allocate(vm, sizeof(type), object_kind)

#define ALLOC_OBJECT_EX(vm, type, extra_size, object_kind)                                         \
  (type *)mesche_object_allocate(vm, sizeof(type) + extra_size, object_kind)

#endif
