#ifndef mesche_module_h
#define mesche_module_h

#include "object.h"
#include "vm.h"

void mesche_module_print_name(ObjectModule *module);
ObjectModule *mesche_module_resolve_by_name(VM *vm, ObjectString *module_name);
ObjectModule *mesche_module_resolve_by_name_string(VM *vm, const char *module_name);
void mesche_module_import(VM *vm, ObjectModule *from_module, ObjectModule *to_module);

#endif
