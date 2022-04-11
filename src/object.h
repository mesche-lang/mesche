#ifndef mesche_object_h
#define mesche_object_h

#include "chunk.h"
#include "value.h"
#include "vm.h"

#define IS_OBJECT(value) ((value).kind == VALUE_OBJECT)
#define AS_OBJECT(value) ((value).as.object)

#define OBJECT_VAL(value) ((Value){VALUE_OBJECT, {.object = (Object *)value}})
#define OBJECT_KIND(value) (AS_OBJECT(value)->kind)

#define IS_CONS(value) mesche_object_is_kind(value, ObjectKindCons)
#define AS_CONS(value) ((ObjectCons *)AS_OBJECT(value))

#define IS_ARRAY(value) mesche_object_is_kind(value, ObjectKindArray)
#define AS_ARRAY(value) ((ObjectArray *)AS_OBJECT(value))

#define IS_FUNCTION(value) mesche_object_is_kind(value, ObjectKindFunction)
#define AS_FUNCTION(value) ((ObjectFunction *)AS_OBJECT(value))

#define IS_SYMBOL(value) mesche_object_is_kind(value, ObjectKindSymbol)
#define AS_SYMBOL(value) ((ObjectSymbol *)AS_OBJECT(value))

#define IS_KEYWORD(value) mesche_object_is_kind(value, ObjectKindKeyword)
#define AS_KEYWORD(value) ((ObjectKeyword *)AS_OBJECT(value))

#define IS_MODULE(value) mesche_object_is_kind(value, ObjectKindModule)
#define AS_MODULE(value) ((ObjectModule *)AS_OBJECT(value))

#define IS_POINTER(value) mesche_object_is_kind(value, ObjectKindPointer)
#define AS_POINTER(value) ((ObjectPointer *)AS_OBJECT(value))

#define IS_CLOSURE(value) mesche_object_is_kind(value, ObjectKindClosure)
#define AS_CLOSURE(value) ((ObjectClosure *)AS_OBJECT(value))

#define IS_RECORD_TYPE(value) mesche_object_is_kind(value, ObjectKindRecord)
#define AS_RECORD_TYPE(value) ((ObjectRecord *)AS_OBJECT(value))

#define IS_RECORD_FIELD(value) mesche_object_is_kind(value, ObjectKindRecordField)
#define AS_RECORD_FIELD(value) ((ObjectRecordField *)AS_OBJECT(value))

#define IS_RECORD_FIELD_ACCESSOR(value) mesche_object_is_kind(value, ObjectKindRecordFieldAccessor)
#define AS_RECORD_FIELD_ACCESSOR(value) ((ObjectRecordFieldAccessor *)AS_OBJECT(value))

#define IS_RECORD_FIELD_SETTER(value) mesche_object_is_kind(value, ObjectKindRecordFieldSetter)
#define AS_RECORD_FIELD_SETTER(value) ((ObjectRecordFieldSetter *)AS_OBJECT(value))

#define IS_RECORD_INSTANCE(value) mesche_object_is_kind(value, ObjectKindRecordInstance)
#define AS_RECORD_INSTANCE(value) ((ObjectRecordInstance *)AS_OBJECT(value))

#define IS_NATIVE_FUNC(value) mesche_object_is_kind(value, ObjectKindNativeFunction)
#define AS_NATIVE_FUNC(value) (((ObjectNativeFunction *)AS_OBJECT(value))->function)

#define AS_STRING(value) ((ObjectString *)AS_OBJECT(value))
#define AS_CSTRING(value) (((ObjectString *)AS_OBJECT(value))->chars)

typedef enum {
  ObjectKindString,
  ObjectKindSymbol,
  ObjectKindKeyword,
  ObjectKindCons,
  ObjectKindArray,
  ObjectKindUpvalue,
  ObjectKindFunction,
  ObjectKindClosure,
  ObjectKindNativeFunction,
  ObjectKindPointer,
  ObjectKindModule,
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

struct ObjectString {
  struct Object object;
  uint32_t hash;
  int length;
  char chars[];
};

struct ObjectSymbol {
  // A symbol is basically a tagged string
  struct ObjectString string;
};

struct ObjectKeyword {
  // A keyword is basically a tagged string
  struct ObjectString string;
};

struct ObjectCons {
  struct Object object;
  Value car;
  Value cdr;
};

struct ObjectArray {
  struct Object object;
  ValueArray objects;
};

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

struct ObjectFunction {
  Object object;
  FunctionType type;
  int arity;
  int rest_arg_index;
  int upvalue_count;
  Chunk chunk;
  KeywordArgumentArray keyword_args;
  ObjectString *name;
};

struct ObjectUpvalue {
  Object object;
  Value *location;
  Value closed;
  struct ObjectUpvalue *next;
};

struct ObjectClosure {
  Object object;
  ObjectModule *module;
  ObjectFunction *function;
  ObjectUpvalue **upvalues;
  int upvalue_count;
};

struct ObjectModule {
  Object object;
  Table locals;
  ValueArray imports;
  ValueArray exports;
  ObjectString *name;
  ObjectFunction *init_function;
};

struct ObjectRecordFieldAccessor {
  Object object;
  int field_index;
  ObjectRecord *record_type;
};

struct ObjectRecordFieldSetter {
  Object object;
  int field_index;
  ObjectRecord *record_type;
};

struct ObjectRecordField {
  Object object;
  Value default_value;
  int field_index;
  ObjectString *name;
};

struct ObjectRecord {
  Object object;
  ValueArray fields;
  ObjectString *name;
};

struct ObjectRecordInstance {
  Object object;
  ValueArray field_values;
  ObjectRecord *record_type;
};

typedef struct {
  Object object;
  FunctionPtr function;
} ObjectNativeFunction;

typedef struct {
  const char *name;
  ObjectFreePtr free_func;
} ObjectPointerType;

typedef struct {
  Object object;
  bool is_managed;
  void *ptr;
  ObjectPointerType *type;
} ObjectPointer;

ObjectString *mesche_object_make_string(VM *vm, const char *chars, int length);
ObjectSymbol *mesche_object_make_symbol(VM *vm, const char *chars, int length);
ObjectKeyword *mesche_object_make_keyword(VM *vm, const char *chars, int length);
ObjectCons *mesche_object_make_cons(VM *vm, Value car, Value cdr);
ObjectArray *mesche_object_make_array(VM *vm);
ObjectUpvalue *mesche_object_make_upvalue(VM *vm, Value *slot);
ObjectFunction *mesche_object_make_function(VM *vm, FunctionType type);
void mesche_object_function_keyword_add(MescheMemory *mem, ObjectFunction *function,
                                        KeywordArgument keyword_arg);
ObjectClosure *mesche_object_make_closure(VM *vm, ObjectFunction *function, ObjectModule *module);
ObjectNativeFunction *mesche_object_make_native_function(VM *vm, FunctionPtr function);
ObjectPointer *mesche_object_make_pointer(VM *vm, void *ptr, bool is_managed);
ObjectPointer *mesche_object_make_pointer_type(VM *vm, void *ptr, ObjectPointerType *type);
ObjectModule *mesche_object_make_module(VM *vm, ObjectString *name);
ObjectRecord *mesche_object_make_record(VM *vm, ObjectString *name);
ObjectRecordField *mesche_object_make_record_field(VM *vm, ObjectString *name, Value default_value);
ObjectRecordFieldAccessor *mesche_object_make_record_accessor(VM *vm, ObjectRecord *record_type,
                                                              int field_index);
ObjectRecordFieldSetter *mesche_object_make_record_setter(VM *vm, ObjectRecord *record_type,
                                                          int field_index);
ObjectRecordInstance *mesche_object_make_record_instance(VM *vm, ObjectRecord *record_type);

void mesche_object_free(VM *vm, struct Object *object);
void mesche_object_print(Value value);

bool mesche_object_is_kind(Value value, ObjectKind kind);
bool mesche_object_string_equalsp(Object *left, Object *right);

#endif
