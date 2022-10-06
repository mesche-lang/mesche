#ifndef mesche_module_h
#define mesche_module_h

#include "function.h"
#include "object.h"
#include "string.h"
#include "table.h"
#include "value.h"
#include "vm.h"

typedef struct ObjectModule {
  Object object;
  Table locals;
  ValueArray imports;
  ValueArray exports;
  ObjectString *name;
  ObjectFunction *init_function;
  bool needs_init;
} ObjectModule;

#define IS_MODULE(value) mesche_object_is_kind(value, ObjectKindModule)
#define AS_MODULE(value) ((ObjectModule *)AS_OBJECT(value))

ObjectModule *mesche_object_make_module(VM *vm, ObjectString *name);
void mesche_free_module(VM *vm, ObjectModule *module);
void mesche_module_print_name(ObjectModule *module);
ObjectModule *mesche_module_resolve_by_name(VM *vm, ObjectString *module_name, bool run_init);
ObjectModule *mesche_module_resolve_by_name_string(VM *vm, const char *module_name, bool run_init);
void mesche_module_import(VM *vm, ObjectModule *from_module, ObjectModule *to_module);

#endif
