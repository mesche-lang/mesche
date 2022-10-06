#ifndef mesche_record_h
#define mesche_record_h

#include "object.h"
#include "string.h"
#include "vm.h"

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

typedef struct ObjectRecord {
  Object object;
  ValueArray fields;
  ObjectString *name;
} ObjectRecord;

typedef struct ObjectRecordFieldAccessor {
  Object object;
  int field_index;
  ObjectRecord *record_type;
} ObjectRecordFieldAccessor;

typedef struct ObjectRecordFieldSetter {
  Object object;
  int field_index;
  ObjectRecord *record_type;
} ObjectRecordFieldSetter;

typedef struct ObjectRecordField {
  Object object;
  Value default_value;
  int field_index;
  ObjectString *name;
} ObjectRecordField;

typedef struct ObjectRecordInstance {
  Object object;
  ValueArray field_values;
  ObjectRecord *record_type;
} ObjectRecordInstance;

ObjectRecord *mesche_object_make_record(VM *vm, ObjectString *name);
void mesche_free_record(VM *vm, ObjectRecord *record);

ObjectRecordField *mesche_object_make_record_field(VM *vm, ObjectString *name, Value default_value);
ObjectRecordFieldAccessor *mesche_object_make_record_accessor(VM *vm, ObjectRecord *record_type,
                                                              int field_index);
ObjectRecordFieldSetter *mesche_object_make_record_setter(VM *vm, ObjectRecord *record_type,
                                                          int field_index);
ObjectRecordInstance *mesche_object_make_record_instance(VM *vm, ObjectRecord *record_type);

#endif
