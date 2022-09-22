#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "array.h"
#include "chunk.h"
#include "compiler.h"
#include "core.h"
#include "disasm.h"
#include "fs.h"
#include "io.h"
#include "list.h"
#include "math.h"
#include "mem.h"
#include "module.h"
#include "object.h"
#include "op.h"
#include "process.h"
#include "string.h"
#include "time.h"
#include "util.h"
#include "value.h"
#include "vm.h"

// NOTE: Enable this for diagnostic purposes
/* #define DEBUG_TRACE_EXECUTION */

void mesche_vm_stack_push(VM *vm, Value value) {
  *vm->stack_top = value;
  vm->stack_top++;
}

Value mesche_vm_stack_pop(VM *vm) {
  vm->stack_top--;
  if (vm->stack_top < vm->stack) {
    PANIC("Value stack has been popped below initial address!");
  }

  return *vm->stack_top;
}

static Value vm_stack_peek(VM *vm, int distance) { return vm->stack_top[-1 - distance]; }

static void vm_reset_stack(VM *vm) {
  vm->stack_top = vm->stack;
  vm->frame_count = 0;
  vm->open_upvalues = NULL;
  vm->current_reset_marker =
      mesche_object_make_stack_marker(vm, STACK_MARKER_RESET, vm->frame_count);
}

static void vm_runtime_error(VM *vm, const char *format, ...) {
  CallFrame *frame = &vm->frames[vm->frame_count - 1];

  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  size_t instruction = frame->ip - frame->closure->function->chunk.code - 1;
  int line = frame->closure->function->chunk.lines[instruction];
  fprintf(stderr, "[line %d] in script\n", line);

  vm_reset_stack(vm);
}

static void vm_free_objects(VM *vm) {
  Object *object = vm->objects;
  vm->objects = NULL;
  while (object != NULL) {
    Object *next = object->next;
    mesche_object_free(vm, object);
    object = next;
  }

  if (vm->gray_stack) {
    free(vm->gray_stack);
  }
  vm->gray_stack = NULL;
}

void mesche_mem_mark_object(VM *vm, Object *object) {
  if (object == NULL)
    return;
  if (object->is_marked)
    return;

#ifdef DEBUG_LOG_GC
  printf("%p    mark    ", object);
  mesche_value_print(OBJECT_VAL(object));
  printf("\n");
#endif

  object->is_marked = true;

  // Add the object to the gray stack if it has references to trace
  if ((object->kind != ObjectKindString && object->kind != ObjectKindSymbol &&
       object->kind != ObjectKindKeyword && object->kind != ObjectKindNativeFunction &&
       object->kind != ObjectKindPointer && object->kind != ObjectKindStackMarker) ||
      (object->kind == ObjectKindPointer && ((ObjectPointer *)object)->type != NULL &&
       ((ObjectPointer *)object)->type->mark_func != NULL)) {
    // Resize the gray stack if necessary (tracks visited objects)
    if (vm->gray_capacity < vm->gray_count + 1) {
      vm->gray_capacity = GROW_CAPACITY(vm->gray_capacity);
      vm->gray_stack = (Object **)realloc(vm->gray_stack, sizeof(Object *) * vm->gray_capacity);

      // Check if something went wrong with allocation
      if (vm->gray_stack == NULL) {
        PANIC("VM's gray stack could not be reallocated.");
      }
    }

    // Add the object to the gray stack
    vm->gray_stack[vm->gray_count++] = object;
  }
}

static void mem_mark_value(VM *vm, Value value) {
  if (IS_OBJECT(value))
    mesche_mem_mark_object(vm, AS_OBJECT(value));
}

static void mem_mark_array(VM *vm, ValueArray *array) {
  for (int i = 0; i < array->count; i++) {
    mem_mark_value(vm, array->values[i]);
  }
}

static void mem_mark_table(VM *vm, Table *table) {
  for (int i = 0; i < table->capacity; i++) {
    Entry *entry = &table->entries[i];
    mesche_mem_mark_object(vm, (Object *)entry->key);
    mem_mark_value(vm, entry->value);
  }
}

static void mem_mark_roots(void *target) {
  VM *vm = (VM *)target;
  for (Value *slot = vm->stack; slot < vm->stack_top; slot++) {
    mem_mark_value(vm, *slot);
  }

  for (int i = 0; i < vm->frame_count; i++) {
    mesche_mem_mark_object(vm, (Object *)vm->frames[i].closure);
  }

  for (ObjectUpvalue *upvalue = vm->open_upvalues; upvalue != NULL; upvalue = upvalue->next) {
    mesche_mem_mark_object(vm, (Object *)upvalue);
  }

  // Mark the current reset stack marker
  mesche_mem_mark_object(vm, (Object *)vm->current_reset_marker);

  // Mark the load path list
  mem_mark_value(vm, OBJECT_VAL(vm->load_paths));

  // Mark roots in every module
  mem_mark_table(vm, &vm->modules);
}

static void mem_darken_object(VM *vm, Object *object) {
#ifdef DEBUG_LOG_GC
  printf("%p    darken ", (void *)object);
  mesche_value_print(OBJECT_VAL(object));
  printf("\n");
#endif

  switch (object->kind) {
  case ObjectKindCons: {
    ObjectCons *cons = (ObjectCons *)object;
    mem_mark_value(vm, cons->car);
    mem_mark_value(vm, cons->cdr);
    break;
  }
  case ObjectKindArray: {
    ObjectArray *array = (ObjectArray *)object;
    mem_mark_array(vm, &array->objects);
    break;
  }
  case ObjectKindClosure: {
    ObjectClosure *closure = (ObjectClosure *)object;
    mesche_mem_mark_object(vm, (Object *)closure->function);
    for (int i = 0; i < closure->upvalue_count; i++) {
      mesche_mem_mark_object(vm, (Object *)closure->upvalues[i]);
    }
    break;
  }
  case ObjectKindContinuation: {
    ObjectContinuation *continuation = (ObjectContinuation *)object;

    // We could be marking the continuation before it's fully initialized, so be
    // careful
    if (continuation->frame_count > 0) {
      for (int i = 0; i < continuation->frame_count; i++) {
        mesche_mem_mark_object(vm, (Object *)continuation->frames[i].closure);
      }

      for (int i = 0; i < continuation->stack_count; i++) {
        mem_mark_value(vm, continuation->stack[i]);
      }
    }

    break;
  }
  case ObjectKindFunction: {
    ObjectFunction *function = (ObjectFunction *)object;
    mesche_mem_mark_object(vm, (Object *)function->name);
    mem_mark_array(vm, &function->chunk.constants);

    // Mark strings associated with keyword arguments
    for (int i = 0; i < function->keyword_args.count; i++) {
      mesche_mem_mark_object(vm, (Object *)function->keyword_args.args[i].name);
    }
    break;
  }
  case ObjectKindPointer: {
    ObjectPointer *pointer = (ObjectPointer *)object;
    if (pointer->type != NULL && pointer->type->mark_func != NULL) {
      pointer->type->mark_func(vm, pointer->ptr);
    }
    break;
  }
  case ObjectKindUpvalue:
    mem_mark_value(vm, ((ObjectUpvalue *)object)->closed);
    break;
  case ObjectKindModule: {
    ObjectModule *module = (ObjectModule *)object;
    mesche_mem_mark_object(vm, (Object *)module->name);
    mem_mark_table(vm, &module->locals);
    mem_mark_array(vm, &module->imports);
    mem_mark_array(vm, &module->exports);
    mesche_mem_mark_object(vm, (Object *)module->init_function);
    break;
  }
  case ObjectKindRecord: {
    ObjectRecord *record = (ObjectRecord *)object;
    mesche_mem_mark_object(vm, (Object *)record->name);
    mem_mark_array(vm, &record->fields);
    break;
  }
  case ObjectKindRecordField: {
    ObjectRecordField *field = (ObjectRecordField *)object;
    mesche_mem_mark_object(vm, (Object *)field->name);
    mem_mark_value(vm, field->default_value);
    break;
  }
  case ObjectKindRecordFieldAccessor: {
    ObjectRecordFieldAccessor *accessor = (ObjectRecordFieldAccessor *)object;
    mesche_mem_mark_object(vm, (Object *)accessor->record_type);
    break;
  }
  case ObjectKindRecordFieldSetter: {
    ObjectRecordFieldSetter *setter = (ObjectRecordFieldSetter *)object;
    mesche_mem_mark_object(vm, (Object *)setter->record_type);
    break;
  }
  case ObjectKindRecordInstance: {
    ObjectRecordInstance *instance = (ObjectRecordInstance *)object;
    mem_mark_array(vm, &instance->field_values);
    mesche_mem_mark_object(vm, (Object *)instance->record_type);
    break;
  }
  default:
    break;
  }
}

static void mem_trace_references(MescheMemory *mem) {
  VM *vm = (VM *)mem;

  // Loop through the stack (which may get more entries added during the loop)
  // to darken all marked objects
  while (vm->gray_count > 0) {
    Object *object = vm->gray_stack[--vm->gray_count];
    mem_darken_object(vm, object);
  }
}

static void mem_table_remove_white(Table *table) {
  for (int i = 0; i < table->capacity; i++) {
    Entry *entry = &table->entries[i];
    if (entry->key != NULL && !entry->key->object.is_marked) {
      mesche_table_delete(table, entry->key);
    }
  }
}

static void mem_sweep_objects(VM *vm) {
  Object *previous = NULL;
  Object *object = vm->objects;

  // Walk through the object linked list
  while (object != NULL) {
    // If the object is marked, move to the next object, retaining
    // a pointer this one so that the next live object can be linked
    // to it
    if (object->is_marked) {
      object->is_marked = false; // Seeya next time...
      previous = object;
      object = object->next;
    } else {
      // If the object is unmarked, remove it from the linked list
      // and free it
      Object *unreached = object;
      object = object->next;
      if (previous != NULL) {
        previous->next = object;
      } else {
        vm->objects = object;
      }

      mesche_object_free(vm, unreached);
    }
  }
}

static void mem_collect_garbage(MescheMemory *mem) {
  VM *vm = (VM *)mem;
  mem_mark_roots(vm);
  if (vm->current_compiler != NULL) {
    mesche_compiler_mark_roots(vm->current_compiler);
  }
  mem_trace_references((MescheMemory *)vm);
  mem_table_remove_white(&vm->strings);
  mem_table_remove_white(&vm->symbols);
  mem_sweep_objects(vm);
}

void mesche_vm_register_core_modules(VM *vm, char *module_path) {
  // Add the module path first so that native functions are loaded in that context
  mesche_vm_load_path_add(vm, module_path);

  mesche_core_module_init(vm);
  mesche_io_module_init(vm);
  mesche_fs_module_init(vm);
  mesche_list_module_init(vm);
  mesche_math_module_init(vm);
  mesche_time_module_init(vm);
  mesche_array_module_init(vm);
  mesche_string_module_init(vm);
  mesche_process_module_init(vm);

  // Load the `mesche core` module to serve as the global scope
  if (mesche_vm_eval_string(vm, "(module-import (mesche core))") != INTERPRET_OK) {
    PANIC("ERROR: Could not load `mesche core` from module path: %s\n\n", module_path);
  }

  Value core_module = mesche_vm_stack_pop(vm);
  if (!IS_MODULE(core_module)) {
    PANIC("ERROR: Unexpected problem loading `mesche core` from module path: %s\n\n", module_path);
  }

  // Store the core module to use in variable lookups
  vm->core_module = AS_MODULE(core_module);
}

void mesche_vm_init(VM *vm, int arg_count, char **arg_array) {
  // Initialize the memory manager
  mesche_mem_init(&vm->mem, mem_collect_garbage);

  // Initialize the gray stack before allocating anything
  vm->gray_count = 0;
  vm->gray_capacity = 0;
  vm->gray_stack = NULL;

  vm->is_running = false;
  vm->objects = NULL;
  vm->load_paths = NULL;
  vm->current_compiler = NULL;
  vm_reset_stack(vm);
  mesche_table_init(&vm->strings);
  mesche_table_init(&vm->symbols);

  // Initialize the module table root module
  mesche_table_init(&vm->modules);
  ObjectString *module_name = mesche_object_make_string(vm, "mesche-user", 11);
  mesche_vm_stack_push(vm, OBJECT_VAL(module_name));
  vm->root_module = mesche_object_make_module(vm, module_name);
  mesche_vm_stack_push(vm, OBJECT_VAL(vm->root_module));
  vm->current_module = vm->root_module;
  mesche_table_set((MescheMemory *)vm, &vm->modules, vm->root_module->name,
                   OBJECT_VAL(vm->root_module));

  // Pop the module and module name
  mesche_vm_stack_pop(vm);
  mesche_vm_stack_pop(vm);

  // Set the program argument variables
  vm->arg_count = arg_count;
  vm->arg_array = arg_array;
}

void mesche_vm_free(VM *vm) {
  // Reset stacks to lose references to things allocated there
  vm_reset_stack(vm);
  vm->open_upvalues = NULL;

  // Do one final GC pass
  mesche_mem_collect_garbage((MescheMemory *)vm);

  // Free remaining roots
  mesche_table_free((MescheMemory *)vm, &vm->modules);
  mesche_table_free((MescheMemory *)vm, &vm->symbols);
  mesche_table_free((MescheMemory *)vm, &vm->strings);
  vm_free_objects(vm);
}

static ObjectUpvalue *vm_capture_upvalue(VM *vm, Value *local) {
  ObjectUpvalue *prev_upvalue = NULL;
  ObjectUpvalue *upvalue = vm->open_upvalues;

  // Loop overall upvalues until we reach a local that is defined on the stack
  // before the local we're trying to capture.  This linked list is in reverse
  // order (top of stack comes first)
  while (upvalue != NULL && upvalue->location > local) {
    prev_upvalue = upvalue;
    upvalue = upvalue->next;
  }

  // If we found an existing upvalue for this local, return it
  if (upvalue != NULL && upvalue->location == local) {
    return upvalue;
  }

  // We didn't find an existing upvalue, create a new one and insert it
  // in the VM's upvalues linked list at the place where the loop stopped
  // (or at the beginning if NULL)
  ObjectUpvalue *created_upvalue = mesche_object_make_upvalue(vm, local);
  created_upvalue->next = upvalue;
  if (prev_upvalue == NULL) {
    // This upvalue is now the first entry
    vm->open_upvalues = created_upvalue;
  } else {
    // Because the captured local can be earlier in the stack than the
    // existing upvalue, we insert it between the previous and current
    // upvalue entries
    prev_upvalue->next = created_upvalue;
  }

  return created_upvalue;
}

static void vm_close_upvalues(VM *vm, Value *stack_slot) {
  // Loop over the upvalues and close any that are at or above the given slot
  // location on the stack
  while (vm->open_upvalues != NULL && vm->open_upvalues->location >= stack_slot) {
    // Copy the value of the local at the given location into the `closed` field
    // and then set `location` to it so that existing code uses the same pointer
    // indirection to access it regardless of whether open or closed
    ObjectUpvalue *upvalue = vm->open_upvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm->open_upvalues = upvalue->next;
  }
}

static bool vm_call(VM *vm, ObjectClosure *closure, uint8_t arg_count, uint8_t keyword_count,
                    bool is_tail_call) {
  // Arity checks differ depending on whether a :rest argument is present
  if (closure->function->rest_arg_index == 0 && arg_count != closure->function->arity) {
    vm_runtime_error(vm, "Expected %d arguments but got %d.", closure->function->arity, arg_count);
    return false;
  } else if (closure->function->rest_arg_index > 0 && arg_count < closure->function->arity - 1) {
    vm_runtime_error(vm, "Expected at least %d arguments but got %d.", closure->function->arity - 1,
                     arg_count);
    return false;
  }

  // Locate the first argument on the value stack
  Value *arg_start = vm->stack_top - (arg_count + (keyword_count * 2));

  // Store the number of keyword arguments the function takes, we'll need it later
  int num_keyword_args = closure->function->keyword_args.count;

  // Process keyword arguments, if any
  if (keyword_count > 0) {
    if (num_keyword_args == 0) {
      vm_runtime_error(vm, "Function does not accept keyword arguments.");
      return false;
    }

    // This is 15 keyword arguments and their values
    Value stored_keyword_args[30];

    // Find keyword arguments and copy them to temporary storage
    Value *keyword_start = arg_start + arg_count;
    Value *keyword_current = keyword_start;
    for (int i = 0; i < keyword_count * 2; i++) {
      // Copy the value to temporary storage
      // TODO: Error if we've reached the storage max
      stored_keyword_args[i] = *keyword_current;
      keyword_current++;
    }

    // Reset the top of the stack to the location of the first keyword argument
    vm->stack_top = keyword_start;

    // Now that we know where keywords start in the value stack, push the
    // keyword default values on so that they line up with the local variables
    // we've defined
    KeywordArgument *keyword_arg = closure->function->keyword_args.args;
    for (int i = 0; i < num_keyword_args; i++) {
      // Check if the passed keyword args match this keyword
      bool found_match = false;
      for (int j = 0; j < keyword_count * 2; j += 2) {
        if (mesche_object_string_equalsp((Object *)keyword_arg->name,
                                         (Object *)AS_KEYWORD(stored_keyword_args[j]))) {
          // Put the value on the stack
          mesche_vm_stack_push(vm, stored_keyword_args[j + 1]);
          found_match = true;
        }
      }

      // Skip to the next keyword if the previous had a match
      if (found_match) {
        keyword_arg++;
        continue;
      }

      // Apply default value of keyword argument
      if (keyword_arg->default_index > 0) {
        mesche_vm_stack_push(
            vm, closure->function->chunk.constants.values[keyword_arg->default_index - 1]);
      } else {
        // If no default value was provided, choose `nil`
        mesche_vm_stack_push(vm, NIL_VAL);
      }

      keyword_arg++;
    }
  } else if (num_keyword_args > 0) {
    // This branch is reached if the function takes keyword arguments but
    // the caller didn't specify any.

    KeywordArgument *keyword_arg = closure->function->keyword_args.args;
    for (int i = 0; i < num_keyword_args; i++) {
      // Apply default value of keyword argument
      if (keyword_arg->default_index > 0) {
        mesche_vm_stack_push(
            vm, closure->function->chunk.constants.values[keyword_arg->default_index - 1]);
      } else {
        // If no default value was provided, choose `nil`
        mesche_vm_stack_push(vm, NIL_VAL);
      }

      keyword_arg++;
    }
  }

  // Only process rest arguments if there is one and the number of passed arguments is
  // clearly enough to trigger it
  if (closure->function->rest_arg_index > 0) {
    if (arg_count >= closure->function->arity) {
      // Collapse all rest arguments into a single list:
      // 1. Create a new cons using the value at rest index, save it back in the same spot
      // 2. Repeat the process until all values have been integrated into the list
      // 3. Shift the keyword argument values to fill the gap

      mesche_vm_stack_push(vm, EMPTY_VAL);
      int rest_value_count = (int)arg_count - closure->function->rest_arg_index + 1;
      int rest_value_start = (int)num_keyword_args + 1;
      for (int i = rest_value_start; i < rest_value_start + rest_value_count; i++) {
        // Allocate the new cons pair in a way that avoids GC collection
        ObjectCons *cons = mesche_object_make_cons(vm, vm_stack_peek(vm, i), vm_stack_peek(vm, 0));
        mesche_vm_stack_pop(vm);
        mesche_vm_stack_push(vm, OBJECT_VAL(cons));
      }

      // Move the new argument list variable into the argument position
      ObjectCons *arg_list = AS_CONS(mesche_vm_stack_pop(vm));
      *(vm->stack_top - (rest_value_start + rest_value_count - 1)) = OBJECT_VAL(arg_list);
      vm->stack_top = arg_start + closure->function->arity + num_keyword_args;

      // Copy the keyword arguments on top of the old value slots that we collapsed
      // into a single slot.
      //
      // NOTE: we're using num_keyword_arguments because values for all keyword
      // arguments will be sent to the call regardless of whether the caller
      // specified them!
      if (num_keyword_args > 0) {
        // arg_count specifies the original argument count which includes the
        // individual rest arguments, so we use it to target the keyword values
        // that were placed after rest arguments.
        memmove(arg_start + closure->function->arity, arg_start + arg_count,
                sizeof(Value) * num_keyword_args);
      }

      // Update the argument count to reflect the reduced amount
      arg_count -= rest_value_count - 1;
    } else {
      // Fill in rest arg with nil if nothing was passed for it
      if (num_keyword_args > 0) {
        // Shift the keyword args forward by 1
        memmove(arg_start + closure->function->arity + 1, arg_start + closure->function->arity,
                sizeof(Value) * num_keyword_args);
      }

      // Place the NIL value and update the stack top
      *(vm->stack_top - num_keyword_args) = NIL_VAL;
      vm->stack_top = arg_start + closure->function->arity + num_keyword_args;
    }
  }

  if (is_tail_call) {
    // Reuse the existing frame
    CallFrame *frame = &vm->frames[vm->frame_count - 1];

    // Close out upvalues for any function locals that have been captured by
    // closures before we wipe them from the stack
    vm_close_upvalues(vm, frame->slots);

    // Copy the new arguments (and the closure value itself) on top of the old
    // slots (add 1 to the argument counts because we're also copying the callee
    // value).
    //
    // NOTE: we're using num_keyword_arguments because values for all keyword
    // arguments will be sent to the call regardless of whether the caller
    // specified them!
    memmove(frame->slots, arg_start - 1, sizeof(Value) * (arg_count + num_keyword_args + 1));

    // Reset the top of the value stack to shrink it back to where it was before
    vm->stack_top = frame->slots + arg_count + num_keyword_args + 1;

    // Set up the closure and instruction pointer to continue execution
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->total_arg_count = arg_count + num_keyword_args;
    return true;
  } else {
    CallFrame *frame = &vm->frames[vm->frame_count++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = arg_start - 1;

    // The total argument count is plain argument count plus number of
    // keyword arguments because we've removed the keywords from the list
    // and left either the specified value or the default.
    frame->total_arg_count = arg_count + num_keyword_args;

    return true;
  }
}

static bool vm_call_value(VM *vm, Value callee, uint8_t arg_count, uint8_t keyword_count,
                          bool is_tail_call) {
  if (IS_OBJECT(callee)) {
    switch (OBJECT_KIND(callee)) {
    case ObjectKindClosure:
      return vm_call(vm, AS_CLOSURE(callee), arg_count, keyword_count, is_tail_call);
    case ObjectKindNativeFunction: {
      FunctionPtr func_ptr = AS_NATIVE_FUNC(callee);
      int total_args = arg_count + keyword_count * 2;
      Value result = func_ptr((MescheMemory *)vm, total_args, vm->stack_top - total_args);

      // Pop off all of the arguments and the function itself
      for (int i = 0; i < total_args + 1; i++) {
        mesche_vm_stack_pop(vm);
      }

      // Push the result to store it
      mesche_vm_stack_push(vm, result);
      return true;
    }
    case ObjectKindRecord: {
      ObjectRecord *record_type = AS_RECORD_TYPE(callee);
      ObjectRecordInstance *instance = mesche_object_make_record_instance(vm, record_type);
      mesche_vm_stack_push(vm, OBJECT_VAL(instance));

      if (arg_count > 0) {
        vm_runtime_error(vm, "Constructor for record type '%s' must be given keyword arguments.",
                         instance->record_type->name->chars);
        return false;
      }

      // Initialize the value array using keyword values or the default for each field
      for (int i = 0; i < record_type->fields.count; i++) {
        bool found_value = false;
        ObjectRecordField *record_field = AS_RECORD_FIELD(record_type->fields.values[i]);

        // Look for the field's value in the keyword parameters
        for (int i = (keyword_count * 2); i >= 0; i -= 2) {
          ObjectKeyword *keyword_arg = AS_KEYWORD(vm_stack_peek(vm, i));
          if (mesche_object_string_equalsp((Object *)record_field->name, (Object *)keyword_arg)) {
            mesche_value_array_write((MescheMemory *)vm, &instance->field_values,
                                     vm_stack_peek(vm, i - 1));
            found_value = true;
            break;
          }
        }

        // If the value wasn't found, use the default
        if (!found_value) {
          mesche_value_array_write((MescheMemory *)vm, &instance->field_values,
                                   record_field->default_value);
        }
      }

      // Pop the record instance off the stack first
      mesche_vm_stack_pop(vm);

      // Pop the record and key/value pairs off the stack
      for (int i = 0; i < (keyword_count * 2) + 1; i++) {
        mesche_vm_stack_pop(vm);
      }

      // Push the record instance back on to the stack as the result
      mesche_vm_stack_push(vm, OBJECT_VAL(instance));
      return true;
    };
    case ObjectKindRecordFieldAccessor: {
      ObjectRecordFieldAccessor *accessor = AS_RECORD_FIELD_ACCESSOR(callee);

      if (arg_count != 1) {
        vm_runtime_error(
            vm, "Record field accessor for type '%s' requires a single record instance argument.",
            accessor->record_type->name->chars);
        return false;
      }

      Value possible_instance = vm_stack_peek(vm, 0);
      if (!IS_OBJECT(possible_instance)) {
        vm_runtime_error(vm, "Expected instance of record type %s but received non-object kind %d.",
                         accessor->record_type->name->chars, possible_instance.kind);
        return false;
      }

      if (!IS_RECORD_INSTANCE(possible_instance)) {
        vm_runtime_error(vm, "Expected instance of record type %s but received object kind %d.",
                         accessor->record_type->name->chars, AS_OBJECT(possible_instance)->kind);
        return false;
      }

      // TODO: Be somewhat tolerant to record type version?
      ObjectRecordInstance *instance = AS_RECORD_INSTANCE(possible_instance);
      if (instance->record_type != accessor->record_type) {
        vm_runtime_error(vm, "Passed record of type %s to accessor that expects %s.",
                         instance->record_type->name->chars, accessor->record_type->name->chars);
        return false;
      }

      // Pop the record and accessor off the stack;
      mesche_vm_stack_pop(vm);
      mesche_vm_stack_pop(vm);

      // Return the value on the stack
      mesche_vm_stack_push(vm, instance->field_values.values[accessor->field_index]);
      return true;
    }
    case ObjectKindRecordFieldSetter: {
      ObjectRecordFieldSetter *setter = AS_RECORD_FIELD_SETTER(callee);

      if (arg_count != 2) {
        vm_runtime_error(vm,
                         "Record field setter for type '%s' requires a record instance argument "
                         "and a value to set for the associated field.",
                         setter->record_type->name->chars);
        return false;
      }

      Value possible_instance = vm_stack_peek(vm, 1);
      if (!IS_OBJECT(possible_instance)) {
        vm_runtime_error(vm, "Expected instance of record type %s but received non-object kind %d.",
                         setter->record_type->name->chars, possible_instance.kind);
        return false;
      }

      if (!IS_RECORD_INSTANCE(possible_instance)) {
        vm_runtime_error(vm, "Expected instance of record type %s but received object kind %d.",
                         setter->record_type->name->chars, AS_OBJECT(possible_instance)->kind);
        return false;
      }

      // TODO: Be somewhat tolerant to record type version?
      ObjectRecordInstance *instance = AS_RECORD_INSTANCE(possible_instance);
      if (instance->record_type != setter->record_type) {
        vm_runtime_error(vm, "Passed record of type %s to setter that expects %s.",
                         instance->record_type->name->chars, setter->record_type->name->chars);
        return false;
      }

      // Pop the setter, record, and value off the stack
      Value value = mesche_vm_stack_pop(vm);
      mesche_vm_stack_pop(vm);
      mesche_vm_stack_pop(vm);

      // Set the field's value and return that same value
      instance->field_values.values[setter->field_index] = value;
      mesche_vm_stack_push(vm, value);
      return true;
    }
    default:
      break; // Value not callable
    }
  }

  vm_runtime_error(vm, "Only functions can be called (received kind %d)", OBJECT_KIND(callee));

  return false;
}

static bool vm_create_module_binding(VM *vm, ObjectModule *module, ObjectString *binding_name,
                                     Value value, bool exported) {
  bool binding_exists = !mesche_table_set((MescheMemory *)vm, &module->locals, binding_name, value);

  if (exported) {
    mesche_value_array_write((MescheMemory *)vm, &module->exports, OBJECT_VAL(binding_name));
  }

  return binding_exists;
}

InterpretResult mesche_vm_run(VM *vm) {
  CallFrame *frame = &vm->frames[vm->frame_count - 1];
  ObjectModule *prev_module = NULL;

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define CURRENT_MODULE() (frame->closure->module ? frame->closure->module : vm->current_module)

// TODO: Don't pop 'a', manipulate top of stack
#define BINARY_OP(value_type, pred, cast, op)                                                      \
  do {                                                                                             \
    if (!pred(vm_stack_peek(vm, 0)) || !pred(vm_stack_peek(vm, 1))) {                              \
      vm_runtime_error(vm, "Operands must be numbers.");                                           \
      return INTERPRET_RUNTIME_ERROR;                                                              \
    }                                                                                              \
    double b = cast(mesche_vm_stack_pop(vm));                                                      \
    double a = cast(mesche_vm_stack_pop(vm));                                                      \
    mesche_vm_stack_push(vm, value_type(a op b));                                                  \
  } while (false)

  vm->is_running = true;

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    bool TRACE_THIS = false;
    const bool TRACE_ALL = true;
    const int TRACE_INSTRUCTIONS[] = {OP_RESET,     OP_SHIFT,  OP_REIFY, OP_CALL,
                                      OP_TAIL_CALL, OP_RETURN, -1};

    if (!TRACE_ALL) {
      TRACE_THIS = false;
      for (int i = 0; TRACE_INSTRUCTIONS[i] != -1; i++) {
        if (*frame->ip == TRACE_INSTRUCTIONS[i]) {
          TRACE_THIS = true;
          break;
        }
      }
    }

    if (TRACE_ALL || TRACE_THIS) {
      printf("\nValue Stack:\n");
      for (Value *slot = vm->stack; slot < vm->stack_top; slot++) {
        printf("  %3d: ", abs(slot - vm->stack_top));
        mesche_value_print(*slot);
        printf("\n");
      }
      printf("\n");

      printf("Call Stack:\n");
      for (int i = 0; i < vm->frame_count; i++) {
        printf("  %3d: ", i);
        mesche_value_print(OBJECT_VAL(vm->frames[i].closure));
        if (i == vm->current_reset_marker->frame_index) {
          printf(" [reset]");
        }
        printf("\n");
      }

      printf("\n");

      printf("Executing:\n");
      mesche_disasm_instr(&frame->closure->function->chunk,
                          (int)(frame->ip - frame->closure->function->chunk.code));
      printf("\n");
    }
#endif

    uint8_t instr;
    Value value;
    ObjectString *name;
    uint8_t slot = 0;

    switch (instr = READ_BYTE()) {
    case OP_NOP:
      // Do nothing!
      break;
    case OP_CONSTANT:
      value = READ_CONSTANT();
      mesche_vm_stack_push(vm, value);
      break;
    case OP_NIL:
      mesche_vm_stack_push(vm, NIL_VAL);
      break;
    case OP_T:
      mesche_vm_stack_push(vm, T_VAL);
      break;
    case OP_POP:
      mesche_vm_stack_pop(vm);
      break;
    case OP_POP_SCOPE: {
      // Only start popping if we have locals to clear
      uint8_t local_count = READ_BYTE();
      if (local_count > 0) {
        Value result = mesche_vm_stack_pop(vm);
        for (int i = 0; i < local_count; i++) {
          mesche_vm_stack_pop(vm);
        }
        mesche_vm_stack_push(vm, result);
      }
      break;
    }
    case OP_CONS: {
      Value car = vm_stack_peek(vm, 1);
      Value cdr = vm_stack_peek(vm, 0);
      Value cons = OBJECT_VAL(mesche_object_make_cons(vm, car, cdr));

      // Pop the car and cdr values off of the stack and push the cons
      mesche_vm_stack_pop(vm);
      mesche_vm_stack_pop(vm);
      mesche_vm_stack_push(vm, cons);

      break;
    }
    case OP_LIST: {
      ObjectCons *list = NULL;
      uint8_t item_count = READ_BYTE();
      if (item_count == 0) {
        mesche_vm_stack_push(vm, EMPTY_VAL);
      } else {
        // List values will be popped in reverse order, build the
        // list back to front (great for cons pairs)
        Value list_value;
        bool pushed_list = false;
        for (int i = 0; i < item_count; i++) {
          list_value = vm_stack_peek(vm, 0);

          if (list) {
            // Push the previous list on the value stack so that it doesn't get
            // collected suddenly when allocating the next cons
            mesche_vm_stack_push(vm, OBJECT_VAL(list));
            pushed_list = true;
          }

          list =
              mesche_object_make_cons(vm, list_value, list == NULL ? EMPTY_VAL : OBJECT_VAL(list));

          // Pop the previous list if needed and then pop the value from the stack
          if (pushed_list) {
            mesche_vm_stack_pop(vm);
          }
          mesche_vm_stack_pop(vm);
        }

        mesche_vm_stack_push(vm, OBJECT_VAL(list));
      }
      break;
    }
    case OP_ADD:
      BINARY_OP(NUMBER_VAL, IS_NUMBER, AS_NUMBER, +);
      break;
    case OP_SUBTRACT:
      BINARY_OP(NUMBER_VAL, IS_NUMBER, AS_NUMBER, -);
      break;
    case OP_MULTIPLY:
      BINARY_OP(NUMBER_VAL, IS_NUMBER, AS_NUMBER, *);
      break;
    case OP_DIVIDE:
      BINARY_OP(NUMBER_VAL, IS_NUMBER, AS_NUMBER, /);
      break;
    case OP_MODULO: {
      if (!IS_NUMBER(vm_stack_peek(vm, 0)) || !IS_NUMBER(vm_stack_peek(vm, 1))) {
        vm_runtime_error(vm, "Operands must be numbers.");
        return INTERPRET_RUNTIME_ERROR;
      }
      int b = (int)AS_NUMBER(mesche_vm_stack_pop(vm));
      int a = (int)AS_NUMBER(mesche_vm_stack_pop(vm));
      mesche_vm_stack_push(vm, NUMBER_VAL(a % b));
      break;
    }
    case OP_NOT:
      mesche_vm_stack_push(vm, IS_NIL(mesche_vm_stack_pop(vm)) ? T_VAL : NIL_VAL);
      break;
    case OP_GREATER_THAN:
      BINARY_OP(BOOL_VAL, IS_NUMBER, AS_NUMBER, >);
      break;
    case OP_GREATER_EQUAL:
      BINARY_OP(BOOL_VAL, IS_NUMBER, AS_NUMBER, >=);
      break;
    case OP_LESS_THAN:
      BINARY_OP(BOOL_VAL, IS_NUMBER, AS_NUMBER, <);
      break;
    case OP_LESS_EQUAL:
      BINARY_OP(BOOL_VAL, IS_NUMBER, AS_NUMBER, <=);
      break;
    case OP_EQUAL:
      // Drop through for now
    case OP_EQV: {
      Value b = mesche_vm_stack_pop(vm);
      Value a = mesche_vm_stack_pop(vm);
      mesche_vm_stack_push(vm, BOOL_VAL(mesche_value_equalp(a, b)));
      break;
    }
    case OP_JUMP: {
      uint16_t offset = READ_SHORT();
      frame->ip += offset;
      break;
    }
    case OP_JUMP_IF_FALSE: {
      uint16_t offset = READ_SHORT();
      if (IS_FALSEY(vm_stack_peek(vm, 0))) {
        frame->ip += offset;
      }
      break;
    }
    case OP_RESET: {
      // Store the previous reset marker, if any, on the value stack
      mesche_vm_stack_push(vm, OBJECT_VAL(vm->current_reset_marker));

      // Create a new stack marker at the current call frame
      vm->current_reset_marker =
          mesche_object_make_stack_marker(vm, STACK_MARKER_RESET, vm->frame_count - 1);

      break;
    }
    case OP_SHIFT: {
      // Find the most recent reset marker
      // TODO: Add support for shift markers for optimizations?
      ObjectStackMarker *reset_marker = vm->current_reset_marker;

      // Locate the reset frame and close out the upvalues of its closure so
      // that they are available when the body is reified later.  We look at
      // the frame *after* the marker's frame_index
      int start_index = reset_marker->frame_index + 1;
      CallFrame *reset_frame = &vm->frames[start_index];
      vm_close_upvalues(vm, reset_frame->slots);

      // Copy frames and values from the marker up to this location into a new
      // continuation object.  Subtract 1 from the slots count so that we don't
      // capture the shift body function which is on the top of the stack!
      ObjectContinuation *continuation = mesche_object_make_continuation(
          vm, reset_frame, vm->frame_count - start_index, reset_frame->slots,
          vm->stack_top - reset_frame->slots - 1);

      // Pop shift body function off the stack before we manipulate it
      Value shift_body_func = mesche_vm_stack_pop(vm);

      // Reset the call and value stacks to the location where OP_RESET was invoked (not including
      // the reset body function)
      vm->frame_count = start_index;
      vm->stack_top = reset_frame->slots;

      // Push the shift body function and continuation object back onto the stack
      mesche_vm_stack_push(vm, shift_body_func);
      mesche_vm_stack_push(vm, OBJECT_VAL(continuation));

      // Create a new unary function that will reify the continuation
      ObjectFunction *function = mesche_object_make_function(vm, TYPE_FUNCTION);
      function->arity = 1;
      mesche_vm_stack_push(vm, OBJECT_VAL(function));

      // Add the continuation as a constant in the function
      int constant =
          mesche_chunk_constant_add((MescheMemory *)vm, &function->chunk, OBJECT_VAL(continuation));

      // Now that the continuation is stored, pop it out of the stack
      mesche_vm_stack_pop(vm);                        // Pop the new function
      mesche_vm_stack_pop(vm);                        // Pop the continuation object
      mesche_vm_stack_push(vm, OBJECT_VAL(function)); // Push the function back

      // Create a new reset context before reifying the continuation so that
      // new OP_SHIFT invocations don't escape it
      mesche_chunk_write((MescheMemory *)vm, &function->chunk, OP_RESET, 0);

      // Load the continuation object from the function constants
      mesche_chunk_write((MescheMemory *)vm, &function->chunk, OP_CONSTANT, 0);
      mesche_chunk_write((MescheMemory *)vm, &function->chunk, 0, 0);

      // Reify the stored continuation where execution will continue
      mesche_chunk_write((MescheMemory *)vm, &function->chunk, OP_REIFY, 0);

      // Return the continuation value normally
      mesche_chunk_write((MescheMemory *)vm, &function->chunk, OP_RETURN, 0);

      // Create a closure for the function since we don't currently support
      // invoking raw functions not wrapped in a closure
      ObjectClosure *closure = mesche_object_make_closure(vm, function, NULL);
      mesche_vm_stack_pop(vm);
      mesche_vm_stack_push(vm, OBJECT_VAL(closure));

      break;
    }
    case OP_REIFY: {
      // Pull the continuation off the top of the stack and find the continuation
      // parameter so that it can be pushed back onto the stack later
      ObjectContinuation *continuation = AS_CONTINUATION(mesche_vm_stack_pop(vm));
      Value marker = mesche_vm_stack_pop(vm);
      Value param = mesche_vm_stack_pop(vm);

      // Put the stack marker and parameter back onto the stack since we found the
      // parameter value now
      mesche_vm_stack_push(vm, param);
      mesche_vm_stack_push(vm, marker);

      // Reify the call stack on top of the current context, including the
      // continuation function itself.  This helps preserve the proper reset
      // context when the reified continuation frames fully return.
      int target_frame_index = vm->frame_count;
      memcpy(&vm->frames[target_frame_index], continuation->frames,
             sizeof(CallFrame) * continuation->frame_count);
      vm->frame_count += continuation->frame_count;

      // Move the instruction pointer of the topmost call frame ahead to skip
      // the OP_CALL instruction after OP_SHIFT.  Why doesn't the OP_CALL move
      // past this on its own?
      vm->frames[vm->frame_count - 1].ip += 3;

      // Append the reified value stack to the stack top
      if (continuation->stack_count > 0) {
        // Calculate the stack offset before we mess with the top pointer
        int stack_offset = vm->stack_top - continuation->frames[0].slots;

        // Copy the continuation values and move the top pointer
        memcpy(vm->stack_top, continuation->stack, sizeof(Value) * continuation->stack_count);
        vm->stack_top += continuation->stack_count;

        // Adjust the slot start pointer of each of the reified frames to ensure
        // that they look for values in the right locations.  Since we're adding
        // the frames to the top of the stack, calculate the offset based on how
        // many values have been added since the first frame was called last.
        for (int i = 0; i < continuation->frame_count; i++) {
          Value *old_value = vm->frames[target_frame_index + i].slots;
          vm->frames[target_frame_index + i].slots += stack_offset;
        }
      }

      // Push the continuation parameter back on the stack again before continuing
      // so that it gets consumed by the topmost frame.
      mesche_vm_stack_push(vm, param);

      // Update the active frame for the next cycle
      frame = &vm->frames[vm->frame_count - 1];

      break;
    }
    case OP_RETURN:
      // Hold on to the function result value before we manipulate the stack
      value = mesche_vm_stack_pop(vm);

      // Close out upvalues for any function locals that have been captured by
      // closures
      vm_close_upvalues(vm, frame->slots);

      // If we're out of call frames, end execution
      vm->frame_count--;
      if (vm->frame_count == 0) {
        // Push the value back on so that it can be read by the REPL
        mesche_vm_stack_pop(vm);
        mesche_vm_stack_push(vm, value);

        // Finish execution
        vm->is_running = false;
        return INTERPRET_OK;
      }

      // Reset the value stack to where it was before this function was called
      vm->stack_top -= frame->total_arg_count;

      // There could be a stray module on the stack if we just executed a script
      // file that defines a module
      // TODO: Add more specific checks
      if (mesche_object_is_kind(vm_stack_peek(vm, 0), ObjectKindModule)) {
        mesche_vm_stack_pop(vm);
      }

      // Restore the previous result value, call frame, and value stack pointer
      // before continuing execution
      mesche_vm_stack_pop(vm); // Pop the closure before restoring result

      // Restore any previous reset marker that was pushed to the stack
      if (IS_RESET_MARKER(vm_stack_peek(vm, 0))) {
        vm->current_reset_marker = AS_STACK_MARKER(mesche_vm_stack_pop(vm));
      }

      mesche_vm_stack_push(vm, value);
      frame = &vm->frames[vm->frame_count - 1];
      break;
    case OP_DISPLAY:
      // Peek at the value on the stack
      mesche_value_print(vm_stack_peek(vm, 0));
      break;
    case OP_LOAD_FILE: {
      ObjectString *path = AS_STRING(vm_stack_peek(vm, 0));
      mesche_vm_load_file(vm, path->chars);

      // Execute the file's closure
      Value *stack_top = vm->stack_top;
      if (vm->stack_top >= stack_top && IS_CLOSURE(vm_stack_peek(vm, 0))) {
        ObjectClosure *closure = AS_CLOSURE(vm_stack_peek(vm, 0));
        if (closure->function->type == TYPE_SCRIPT) {
          // Call the script
          vm_call(vm, closure, 0, 0, false);
          frame = &vm->frames[vm->frame_count - 1];
        }
      }

      // Pop the file path off the stack, but not the closure!  It needs
      // to be there so that OP_RETURN semantics work correctly.
      Value closure = mesche_vm_stack_pop(vm);
      mesche_vm_stack_pop(vm);
      mesche_vm_stack_push(vm, closure);

      break;
    }
    case OP_DEFINE_RECORD: {
      // Skip all the fields to find the name
      uint8_t field_count = READ_BYTE();

      // Duplicate the record type name's symbol as a string
      ObjectSymbol *name_symbol = AS_SYMBOL(vm_stack_peek(vm, field_count * 2));
      ObjectString *record_name =
          mesche_object_make_string(vm, name_symbol->string.chars, name_symbol->string.length);
      mesche_vm_stack_push(vm, OBJECT_VAL(record_name));

      // Create the record type
      ObjectRecord *record = mesche_object_make_record(vm, record_name);
      mesche_vm_stack_push(vm, OBJECT_VAL(record));

      // Create a binding for the maker function
      char *maker_name =
          mesche_cstring_join("make-", 5, record_name->chars, record_name->length, "");
      ObjectString *maker_name_string =
          mesche_object_make_string(vm, maker_name, strlen(maker_name));
      mesche_vm_stack_push(vm, OBJECT_VAL(maker_name_string));
      free(maker_name);

      // Create the binding for the maker and then pop off the record type and name string
      vm_create_module_binding(vm, CURRENT_MODULE(), maker_name_string, OBJECT_VAL(record), true);
      mesche_vm_stack_pop(vm);
      mesche_vm_stack_pop(vm);
      mesche_vm_stack_pop(vm);

      for (int i = 0; i < field_count; i++) {
        // Build the record field from name and default value
        int stack_pos = ((field_count - i) * 2) - 1;
        ObjectSymbol *name = AS_SYMBOL(vm_stack_peek(vm, stack_pos));
        Value value = vm_stack_peek(vm, stack_pos + 1);
        ObjectRecordField *field = mesche_object_make_record_field(vm, &name->string, value);
        mesche_vm_stack_push(vm, OBJECT_VAL(field));
        mesche_value_array_write((MescheMemory *)vm, &record->fields, OBJECT_VAL(field));
        mesche_vm_stack_pop(vm);

        // Create a binding for the field accessor "function"
        char *accessor_name = mesche_cstring_join(record_name->chars, record_name->length,
                                                  name->string.chars, name->string.length, "-");
        ObjectString *accessor_name_string =
            mesche_object_make_string(vm, accessor_name, strlen(accessor_name));
        mesche_vm_stack_push(vm, OBJECT_VAL(accessor_name_string));
        free(accessor_name);
        ObjectRecordFieldAccessor *accessor = mesche_object_make_record_accessor(vm, record, i);
        mesche_vm_stack_push(vm, OBJECT_VAL(accessor));
        vm_create_module_binding(vm, CURRENT_MODULE(), accessor_name_string, OBJECT_VAL(accessor),
                                 true);

        // Create a binding for the field setter "function"
        // TODO: Only do this if requested!
        char *setter_name = mesche_cstring_join(accessor_name_string->chars,
                                                accessor_name_string->length, "-set!", 5, "");
        ObjectString *setter_name_string =
            mesche_object_make_string(vm, setter_name, strlen(setter_name));
        mesche_vm_stack_push(vm, OBJECT_VAL(setter_name_string));
        free(setter_name);
        ObjectRecordFieldSetter *setter = mesche_object_make_record_setter(vm, record, i);
        mesche_vm_stack_push(vm, OBJECT_VAL(setter));
        vm_create_module_binding(vm, CURRENT_MODULE(), setter_name_string, OBJECT_VAL(setter),
                                 true);

        // Pop the accessor and setter functions and names off of the stack
        mesche_vm_stack_pop(vm);
        mesche_vm_stack_pop(vm);
        mesche_vm_stack_pop(vm);
        mesche_vm_stack_pop(vm);
      }

      // Pop all of the fields and record name off of the stack (basically, the
      // arguments to `define-record-type`)
      for (int i = 0; i < (field_count * 2) + 1; i++) {
        mesche_vm_stack_pop(vm);
      }

      // Push the record type onto the stack as the result
      mesche_vm_stack_push(vm, OBJECT_VAL(record));

      break;
    }
    case OP_DEFINE_MODULE: {
      // Resolve the module and set the current closure's module.
      // This works because scripts are compiled into functions with
      // their own closure, so a `define-module` will cause that to
      // be set.
      ObjectString *module_name = AS_STRING(mesche_vm_stack_pop(vm));
      // TODO: Error if module is already set?
      frame->closure->module = mesche_module_resolve_by_name(vm, module_name);

      // Push the defined module onto the stack
      mesche_vm_stack_push(vm, OBJECT_VAL(frame->closure->module));
      break;
    }
    case OP_RESOLVE_MODULE: {
      // Resolve the module based on the given path
      ObjectString *module_name = AS_STRING(mesche_vm_stack_pop(vm));
      ObjectModule *resolved_module = mesche_module_resolve_by_name(vm, module_name);

      // Compiling the module file will push its closure onto the stack, but we
      // can't accept any closure, need to make sure it matches the one in the
      // returned module
      if (resolved_module) {
        // If the module is resolved but doesn't have an init function to
        // execute, handle it directly
        if (!IS_CLOSURE(vm_stack_peek(vm, 0)) ||
            AS_CLOSURE(vm_stack_peek(vm, 0))->function != resolved_module->init_function) {
          // This case will happen when the module has previously been executed
          // and was merely resolved this time.  We have to emulate the load of
          // a module to keep the stack handling consistent, so push the resolved
          // module and a nil val so that `OP_IMPORT_MODULE` doesn't have to the
          // stack first!
          mesche_vm_stack_push(vm, OBJECT_VAL(resolved_module));
          mesche_vm_stack_push(vm, NIL_VAL);
        } else {
          ObjectClosure *closure = AS_CLOSURE(vm_stack_peek(vm, 0));
          if (closure->function->type == TYPE_SCRIPT) {
            // Set up the module's closure for execution in the next VM loop and
            // reset the frame back to the current one until the next cycle
            vm_call(vm, closure, 0, 0, false);
            frame = &vm->frames[vm->frame_count - 1];
          }

          // Pop the closure off the stack and push the module on before it so
          // that it can be imported after the closure finishes executing
          mesche_vm_stack_pop(vm);
          mesche_vm_stack_push(vm, OBJECT_VAL(resolved_module));
          mesche_vm_stack_push(vm, OBJECT_VAL(closure));
        }
      } else {
        // TODO: What if it didn't get resolved?  Will it ever happen?
      }
      break;
    }
    case OP_IMPORT_MODULE: {
      // First, pop the result of evaluating the module's body because we don't
      // need it.
      mesche_vm_stack_pop(vm);

      // Grab the module that was resolved and pull in its exports
      ObjectModule *imported_module = AS_MODULE(vm_stack_peek(vm, 0));
      mesche_module_import(vm, imported_module, CURRENT_MODULE());

      // Module import can happen in two ways:
      // - Via imports in `define-module`
      // - By calling `module-import`
      // We do not explicitly pop the module off of the stack here because
      // the former case emits its own OP_POP and the latter case uses normal
      // block expression semantics to either pop off the value or return it
      // when evaluating the expression.
      //
      // In other words, leave the module on the stack!
      break;
    }
    case OP_ENTER_MODULE: {
      ObjectString *module_name = AS_STRING(mesche_vm_stack_pop(vm));
      ObjectModule *module = mesche_module_resolve_by_name(vm, module_name);
      vm->current_module = module;
      mesche_vm_stack_push(vm, OBJECT_VAL(module));
      break;
    }
    case OP_EXPORT_SYMBOL:
      name = READ_STRING();
      // TODO: Convert the local value for this binding to an ObjectExport
      mesche_value_array_write((MescheMemory *)vm, &CURRENT_MODULE()->exports, OBJECT_VAL(name));
      break;
    case OP_DEFINE_GLOBAL:
      name = READ_STRING();
      mesche_table_set((MescheMemory *)vm, &CURRENT_MODULE()->locals, name, vm_stack_peek(vm, 0));
      break;
    case OP_READ_GLOBAL:
      name = READ_STRING();

      // First, try to loop up the variable in the current module's locals
      if (!mesche_table_get(&CURRENT_MODULE()->locals, name, &value)) {
        // Then, look up the variable in the core module's locals
        if (!mesche_table_get(&vm->core_module->locals, name, &value)) {
          vm_runtime_error(vm, "Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
      }

      mesche_vm_stack_push(vm, value);
      break;
    case OP_READ_UPVALUE:
      slot = READ_BYTE();
      mesche_vm_stack_push(vm, *frame->closure->upvalues[slot]->location);
      break;
    case OP_READ_LOCAL:
      slot = READ_BYTE();
      mesche_vm_stack_push(vm, frame->slots[slot]);
      break;
    case OP_SET_GLOBAL: {
      name = READ_STRING();
      Table *globals = &CURRENT_MODULE()->locals;
      if (mesche_table_set((MescheMemory *)vm, globals, name, vm_stack_peek(vm, 0))) {
        vm_runtime_error(vm, "Undefined variable '%s'.", name->chars);
        return INTERPRET_RUNTIME_ERROR;
      }
      break;
    }
    case OP_SET_UPVALUE:
      slot = READ_BYTE();
      *frame->closure->upvalues[slot]->location = vm_stack_peek(vm, 0);
      break;
    case OP_SET_LOCAL:
      slot = READ_BYTE();
      frame->slots[slot] = vm_stack_peek(vm, 0);
      break;
    case OP_CALL: {
      // Call the function with the specified number of arguments
      uint8_t arg_count = READ_BYTE();
      uint8_t keyword_count = READ_BYTE();
      if (!vm_call_value(vm, vm_stack_peek(vm, arg_count + (keyword_count * 2)), arg_count,
                         keyword_count, false)) {
        return INTERPRET_RUNTIME_ERROR;
      }

      // Set the current frame to the new call frame
      frame = &vm->frames[vm->frame_count - 1];
      break;
    }
    case OP_TAIL_CALL: {
      // Call the function with the specified number of arguments
      uint8_t arg_count = READ_BYTE();
      uint8_t keyword_count = READ_BYTE();
      if (!vm_call_value(vm, vm_stack_peek(vm, arg_count + (keyword_count * 2)), arg_count,
                         keyword_count, true)) {
        return INTERPRET_RUNTIME_ERROR;
      }

      // Retain the current call frame, it has already been updated
      frame = &vm->frames[vm->frame_count - 1];
      break;
    }
    case OP_CLOSURE: {
      ObjectFunction *function = AS_FUNCTION(READ_CONSTANT());
      ObjectClosure *closure = mesche_object_make_closure(vm, function, CURRENT_MODULE());
      mesche_vm_stack_push(vm, OBJECT_VAL(closure));

      for (int i = 0; i < closure->upvalue_count; i++) {
        // If the upvalue is a local, capture it explicitly.  Otherwise,
        // grab a handle to the parent upvalue we're pointing to.
        uint8_t is_local = READ_BYTE();
        uint8_t index = READ_BYTE();

        if (is_local) {
          closure->upvalues[i] = vm_capture_upvalue(vm, frame->slots + index);
        } else {
          closure->upvalues[i] = frame->closure->upvalues[index];
        }
      }

      break;
    }
    case OP_CLOSE_UPVALUE: {
      // NOTE: This opcode gets issued when a scope block is ending (usually
      // from a `let` or `begin` expression with multiple body expressions)
      // so skip the topmost value on the stack (the last expression result)
      // and grab the next value which should be the first local.
      vm_close_upvalues(vm, vm->stack_top - 2);

      // Move the result value into the local's spot
      Value result = mesche_vm_stack_pop(vm);
      mesche_vm_stack_pop(vm);
      mesche_vm_stack_push(vm, result);

      break;
    }
    case OP_APPLY: {
      // Grab the function to call and the list to call it on
      Value func_value = vm_stack_peek(vm, 1);
      Value list_value = vm_stack_peek(vm, 0);

      // Are they the expected types?
      if (!IS_CLOSURE(func_value) && !IS_NATIVE_FUNC(func_value)) {
        vm_runtime_error(vm, "Cannot apply non-function value.");
        return INTERPRET_RUNTIME_ERROR;
      } else if (!IS_CONS(list_value)) {
        vm_runtime_error(vm, "Cannot apply function to non-list value.");
        return INTERPRET_RUNTIME_ERROR;
      }

      // Store the list value location so that we can overwrite it after
      // unrolling the values.  We do this to ensure the list doesn't get freed
      // during a GC pass that might occur while pushing the individual values
      // to the stack.
      int arg_count = 0;
      Value *list_value_slot = vm->stack_top - 1;

      // Unroll the list onto the stack
      while (IS_CONS(list_value)) {
        ObjectCons *cons = AS_CONS(list_value);
        mesche_vm_stack_push(vm, cons->car);
        arg_count++;
        list_value = cons->cdr;
      }

      // Remove the original list value by shifting all new values down by 1
      memmove(list_value_slot, list_value_slot + 1, sizeof(Value) * arg_count);
      vm->stack_top--;

      // Call the function with the unrolled argument list
      // TODO: Can we make this a tail call?
      if (!vm_call_value(vm, func_value, arg_count, 0, false)) {
        return INTERPRET_RUNTIME_ERROR;
      }

      // Set the current frame to the new call frame
      frame = &vm->frames[vm->frame_count - 1];

      break;
    }
    }

#ifdef DEBUG_TRACE_EXECUTION
    if (TRACE_THIS) {
      printf("Value Stack After:\n");
      for (Value *slot = vm->stack; slot < vm->stack_top; slot++) {
        printf("  %3d: ", abs(slot - vm->stack_top));
        mesche_value_print(*slot);
        printf("\n");
      }
      printf("\n");

      printf("Call Stack After:\n");
      for (int i = 0; i < vm->frame_count; i++) {
        printf("  %3d: ", i);
        mesche_value_print(OBJECT_VAL(vm->frames[i].closure));
        if (i == vm->current_reset_marker->frame_index) {
          printf(" [reset]");
        }
        printf("\n");
      }

      printf("\n--- end of instr --- \n");
    }
#endif
  }

  vm->is_running = false;

#undef READ_STRING
#undef READ_SHORT
#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}

static Value mesche_vm_clock_native(int arg_count, Value *args) {
  return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

void mesche_vm_define_native(VM *vm, ObjectModule *module, const char *name, FunctionPtr function,
                             bool exported) {
  // Create objects for the name and the function
  ObjectString *func_name = mesche_object_make_string(vm, name, (int)strlen(name));
  mesche_vm_stack_push(vm, OBJECT_VAL(func_name));
  mesche_vm_stack_push(vm, OBJECT_VAL(mesche_object_make_native_function(vm, function)));

  // Create the binding to the function
  vm_create_module_binding(vm, module, AS_STRING(*(vm->stack_top - 2)), *(vm->stack_top - 1),
                           exported);

  // Pop the values we stored temporarily
  mesche_vm_stack_pop(vm);
  mesche_vm_stack_pop(vm);
}

void mesche_vm_define_native_funcs(VM *vm, const char *module_name,
                                   MescheNativeFuncDetails *func_array) {
  ObjectModule *module = mesche_module_resolve_by_name_string(vm, module_name);
  MescheNativeFuncDetails *func_details = func_array;

  for (;;) {
    // We determine when to exit by checking for an entry with a null name
    if (func_details->name == NULL) {
      break;
    }

    // Create objects for the name and the function
    ObjectString *func_name =
        mesche_object_make_string(vm, func_details->name, (int)strlen(func_details->name));
    mesche_vm_stack_push(vm, OBJECT_VAL(func_name));
    mesche_vm_stack_push(
        vm, OBJECT_VAL(mesche_object_make_native_function(vm, func_details->function)));

    // Create the binding to the function
    vm_create_module_binding(vm, module, AS_STRING(*(vm->stack_top - 2)), *(vm->stack_top - 1),
                             func_details->exported);

    // Pop the values we stored temporarily
    mesche_vm_stack_pop(vm);
    mesche_vm_stack_pop(vm);

    // Move on to the next entry
    func_details++;
  }
}

void mesche_vm_load_path_add(VM *vm, const char *load_path) {
  char *resolved_path = mesche_fs_resolve_path(load_path);
  if (resolved_path) {
    ObjectString *path_str = mesche_object_make_string(vm, resolved_path, strlen(resolved_path));
    mesche_vm_stack_push(vm, OBJECT_VAL(path_str));
    vm->load_paths = mesche_list_push(vm, vm->load_paths, OBJECT_VAL(path_str));
    mesche_vm_stack_pop(vm);
    free(resolved_path);
  } else {
    printf("Could not resolve load path: %s\n", load_path);
  }
}

InterpretResult mesche_vm_eval_string(VM *vm, const char *script_string) {
  ObjectFunction *function = mesche_compile_source(vm, script_string);
  if (function == NULL) {
    return INTERPRET_COMPILE_ERROR;
  }

  // Push the top-level function as a closure
  mesche_vm_stack_push(vm, OBJECT_VAL(function));
  ObjectClosure *closure = mesche_object_make_closure(vm, function, NULL);
  mesche_vm_stack_pop(vm);
  mesche_vm_stack_push(vm, OBJECT_VAL(closure));

  // Only run the VM if it isn't already running
  if (!vm->is_running) {
    // Call the initial closure and run the VM
    vm_call(vm, closure, 0, 0, false);
    return mesche_vm_run(vm);
  }
}

InterpretResult mesche_vm_load_module(VM *vm, ObjectModule *module, const char *module_path) {
  char *source = mesche_fs_file_read_all(module_path);
  if (source == NULL) {
    // TODO: Report and fail gracefully
    PANIC("ERROR: Could not load script file: %s\n\n", module_path);
  }

  module = mesche_compile_module(vm, module, source);
  if (module == NULL) {
    return INTERPRET_COMPILE_ERROR;
  }

  // Push the top-level function as a closure but do not execute it yet!
  // The OP_IMPORT_MODULE instruction will check for a new script closure
  // at the top of the value stack and will execute it if found.
  ObjectClosure *closure = mesche_object_make_closure(vm, module->init_function, NULL);
  closure->module = module;
  mesche_vm_stack_push(vm, OBJECT_VAL(closure));

  // Free the module source
  free(source);

  if (!vm->is_running) {
    // Call the initial closure and run the VM
    vm_call(vm, closure, 0, 0, false);
    InterpretResult result = mesche_vm_run(vm);

    // This code path *SHOULD* only get called when the developer has used
    // `mesche_vm_define_native`, all other module imports are handled with the
    // OP_IMPORT_MODULE instruction at runtime.  Thus, pop the name string off
    // of the value stack.
    mesche_vm_stack_pop(vm);

    return result;
  }

  // Since we can't run the module body yet, return that things are OK
  return INTERPRET_OK;
}

InterpretResult mesche_vm_load_file(VM *vm, const char *file_path) {
  char *source = mesche_fs_file_read_all(file_path);
  if (source == NULL) {
    // TODO: Report and fail gracefully
    PANIC("ERROR: Could not load script file: %s\n\n", file_path);
  }

  // Eval the script and then free the source string
  InterpretResult result = mesche_vm_eval_string(vm, source);
  free(source);

  if (result == INTERPRET_COMPILE_ERROR) {
    vm_runtime_error(vm, "Could not load file due to compilation error: %s\n", file_path);
    return result;
  }

  return result;
}
