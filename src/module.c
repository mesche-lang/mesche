#include <linux/limits.h>
#include <stdlib.h>

#include "fs.h"
#include "module.h"
#include "object.h"
#include "string.h"
#include "table.h"
#include "util.h"
#include "vm-impl.h"

ObjectModule *mesche_object_make_module(VM *vm, ObjectString *name) {
  ObjectModule *module = ALLOC_OBJECT(vm, ObjectModule, ObjectKindModule);
  module->name = name;
  module->init_function = NULL;
  module->needs_init = false;

  // Initialize binding tables
  mesche_table_init(&module->locals);
  mesche_value_array_init(&module->imports);
  mesche_value_array_init(&module->exports);

  return module;
}

void mesche_free_module(VM *vm, ObjectModule *module) {
  mesche_table_free((MescheMemory *)vm, &module->locals);
  mesche_value_array_free((MescheMemory *)vm, &module->imports);
  mesche_value_array_free((MescheMemory *)vm, &module->exports);
  FREE(vm, ObjectModule, module);
}

void mesche_module_print_name(ObjectModule *module) { printf("(%s)", module->name->chars); }

// (mesche io) -> modules/mesche/io.msc
// (substratic graphics texture) -> deps/substratic/graphics/texture.msc

char *mesche_module_make_path(const char *base_path, const char *module_name) {
  int start_index = strlen(base_path);
  char *module_path = malloc(sizeof(char) * 1000);

  memcpy(module_path, base_path, start_index);
  module_path[start_index++] = '/';

  for (int i = 0; i < strlen(module_name); i++) {
    module_path[start_index++] = module_name[i] == ' ' ? '/' : module_name[i];
  }

  // Add the file extension to the last part of the path
  memcpy(module_path + start_index, ".msc", 4);
  module_path[start_index + 4] = 0;

  return module_path;
}

char *mesche_module_find_module_path(VM *vm, const char *module_name) {
  // TODO: Check for path variations --
  // mesche/io.msc
  // mesche/io/module.msc

  ObjectCons *load_path_entry = vm->load_paths;
  while (load_path_entry) {
    char *module_path = mesche_module_make_path(AS_CSTRING(load_path_entry->car), module_name);
    if (mesche_fs_path_exists_p(module_path)) {
      return module_path;
    }

    if (module_path) {
      free(module_path);
    }

    // Search the next load path
    load_path_entry = IS_CONS(load_path_entry->cdr) ? AS_CONS(load_path_entry->cdr) : NULL;
  }

  return NULL;
}

ObjectModule *mesche_module_resolve_by_name(VM *vm, ObjectString *module_name, bool run_init) {
  // First, check if the module is already loaded, return it if so
  // Then check the load path to see if a module file exists
  // If no file exists, create a new module with the name

  // Execution cases:
  // - Resolving module that is not yet created
  // - Resolving module that is not yet created and will never have an init function
  // - Resolving module that is already created but not yet initialized
  // - Resolving module that is created and initialized

  Value module_val = FALSE_VAL;
  ObjectModule *module = NULL;

  // Has the module already been created somehow?
  if (mesche_table_get(&vm->modules, module_name, &module_val)) {
    module = AS_MODULE(module_val);
  } else {
    // Create a new module and add it to the table but push to stack first to
    // avoid collection
    module = mesche_object_make_module(vm, module_name);
    mesche_vm_stack_push(vm, OBJECT_VAL(module));
    mesche_table_set((MescheMemory *)vm, &vm->modules, module_name, OBJECT_VAL(module));
    mesche_vm_stack_pop(vm);

    // The module will need to be initialized
    module->needs_init = true;
  }

  // If the module needs to be initialized, do it if requested
  if (run_init && module->needs_init) {
    char *module_path = mesche_module_find_module_path(vm, module_name->chars);
    if (module_path) {
      // Load the module file to populate the empty module
      // TODO: Guard against circular module loads!
      module->needs_init = false;
      InterpretResult result = mesche_vm_load_module(vm, module, module_path);

      if (result != INTERPRET_OK) {
        PANIC("Error while loading module path %s\n", module_path)
      }

      free(module_path);
    } else {
      // TODO: Should I log a message?
      module->needs_init = false;
    }
  }

  return module;
}

ObjectModule *mesche_module_resolve_by_name_string(VM *vm, const char *module_name, bool run_init) {
  // Allocate the name string and push it to the stack temporarily to avoid GC
  ObjectString *module_name_str = mesche_object_make_string(vm, module_name, strlen(module_name));
  ObjectModule *module = mesche_module_resolve_by_name(vm, module_name_str, run_init);

  return module;
}

void mesche_module_import(VM *vm, ObjectModule *from_module, ObjectModule *to_module) {
  // TODO: Warn or error on shadowing?
  // Look up the value for each exported symbol and bind it in the current module
  for (int i = 0; i < from_module->exports.count; i++) {
    Value export_value = FALSE_VAL;
    ObjectString *export_name = AS_STRING(from_module->exports.values[i]);
    mesche_table_get(&from_module->locals, export_name, &export_value);
    mesche_table_set((MescheMemory *)vm, &to_module->locals, export_name, export_value);
  }
}
