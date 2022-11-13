#include "mem.h"
#include "record.h"

ObjectRecord *mesche_object_make_record(VM *vm, ObjectString *name) {
  ObjectRecord *record = ALLOC_OBJECT(vm, ObjectRecord, ObjectKindRecord);
  record->name = name;
  mesche_value_array_init(&record->fields);

  return record;
}

void mesche_free_record(VM *vm, ObjectRecord *record) {
  mesche_value_array_free((MescheMemory *)vm, &record->fields);
  FREE(vm, ObjectRecord, record);
}

ObjectRecordField *mesche_object_make_record_field(VM *vm, ObjectString *name,
                                                   Value default_value) {
  ObjectRecordField *field = ALLOC_OBJECT(vm, ObjectRecordField, ObjectKindRecordField);
  field->name = name;
  field->default_value = default_value;

  return field;
}

ObjectRecordFieldAccessor *mesche_object_make_record_accessor(VM *vm, ObjectRecord *record_type,
                                                              int field_index) {
  ObjectRecordFieldAccessor *accessor =
      ALLOC_OBJECT(vm, ObjectRecordFieldAccessor, ObjectKindRecordFieldAccessor);
  accessor->record_type = record_type;
  accessor->field_index = field_index;

  return accessor;
}

ObjectRecordFieldSetter *mesche_object_make_record_setter(VM *vm, ObjectRecord *record_type,
                                                          int field_index) {
  ObjectRecordFieldSetter *setter =
      ALLOC_OBJECT(vm, ObjectRecordFieldSetter, ObjectKindRecordFieldSetter);
  setter->record_type = record_type;
  setter->field_index = field_index;

  return setter;
}

ObjectRecordInstance *mesche_object_make_record_instance(VM *vm, ObjectRecord *record_type) {
  ObjectRecordInstance *instance = ALLOC_OBJECT(vm, ObjectRecordInstance, ObjectKindRecordInstance);
  instance->record_type = record_type;
  mesche_value_array_init(&instance->field_values);

  return instance;
}

ObjectRecordPredicate *mesche_object_make_record_predicate(VM *vm, ObjectRecord *record_type) {
  ObjectRecordPredicate *predicate =
      ALLOC_OBJECT(vm, ObjectRecordPredicate, ObjectKindRecordPredicate);
  predicate->record_type = record_type;
  return predicate;
}
