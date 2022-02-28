#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "chunk.h"
#include "compiler.h"
#include "disasm.h"
#include "fs.h"
#include "list.h"
#include "mem.h"
#include "module.h"
#include "object.h"
#include "op.h"
#include "process.h"
#include "string.h"
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
}

static void vm_runtime_error(VM *vm, const char *format, ...) {
  CallFrame *frame = &vm->frames[vm->frame_count - 1];

  // TODO: Port to printf
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
  while (object != NULL) {
    Object *next = object->next;
    mesche_object_free(vm, object);
    object = next;
  }

  if (vm->gray_stack) {
    free(vm->gray_stack);
  }
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
  if (object->kind != ObjectKindString && object->kind != ObjectKindSymbol &&
      object->kind != ObjectKindKeyword && object->kind != ObjectKindNativeFunction &&
      object->kind != ObjectKindPointer) {
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

static void mem_mark_module(VM *vm, struct ObjectModule *module) {
  mem_mark_table(vm, &module->locals);
  mem_mark_array(vm, &module->exports);
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
  case ObjectKindClosure: {
    ObjectClosure *closure = (ObjectClosure *)object;
    mesche_mem_mark_object(vm, (Object *)closure->function);
    for (int i = 0; i < closure->upvalue_count; i++) {
      mesche_mem_mark_object(vm, (Object *)closure->upvalues[i]);
    }
    break;
  }
  case ObjectKindFunction: {
    ObjectFunction *function = (ObjectFunction *)object;
    mesche_mem_mark_object(vm, (Object *)function->name);
    mem_mark_array(vm, &function->chunk.constants);
    break;
  }
  case ObjectKindUpvalue:
    mem_mark_value(vm, ((ObjectUpvalue *)object)->closed);
    break;
  case ObjectKindModule:
    mem_mark_module(vm, ((ObjectModule *)object));
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

void mesche_vm_register_core_modules(VM *vm) {
  ObjectModule *module = mesche_module_resolve_by_name_string(vm, "mesche process");
  mesche_vm_define_native(vm, module, "process-arguments", mesche_process_arguments_msc, true);

  module = mesche_module_resolve_by_name_string(vm, "mesche list");
  mesche_vm_define_native(vm, module, "list-nth", mesche_list_nth_msc, true);
}

void mesche_vm_init(VM *vm, int arg_count, char **arg_array) {
  // initialize the memory manager
  mesche_mem_init(&vm->mem, mem_collect_garbage);

  vm->is_running = false;
  vm->objects = NULL;
  vm->current_compiler = NULL;
  vm_reset_stack(vm);
  mesche_table_init(&vm->strings);
  mesche_table_init(&vm->symbols);

  // Initialize the module table root module
  mesche_table_init(&vm->modules);
  ObjectString *module_name = mesche_object_make_string(vm, "mesche-user", 11);
  vm->root_module = mesche_object_make_module(vm, module_name);
  vm->current_module = vm->root_module;
  vm->load_paths = NULL;
  mesche_table_set((MescheMemory *)vm, &vm->modules, vm->root_module->name,
                   OBJECT_VAL(vm->root_module));

  // Set the program argument variables
  vm->arg_count = arg_count;
  vm->arg_array = arg_array;

  // Initialize the gray stack
  vm->gray_count = 0;
  vm->gray_capacity = 0;
  vm->gray_stack = NULL;
}

void mesche_vm_free(VM *vm) {
  mesche_table_free((MescheMemory *)vm, &vm->strings);
  mesche_table_free((MescheMemory *)vm, &vm->symbols);
  mesche_table_free((MescheMemory *)vm, &vm->modules);
  vm_reset_stack(vm);
  vm_free_objects(vm);
}

static bool vm_call(VM *vm, ObjectClosure *closure, uint8_t arg_count) {
  // Need to factor in:
  // - Required argument count (arity)
  // - Number of keywords (check the value stack to match them)
  Value *arg_start = vm->stack_top - arg_count;
  if (closure->function->keyword_args.count > 0) {
    // This is 15 keyword arguments and their values
    Value stored_keyword_args[30];

    // Find keyword arguments and copy them to temporary storage
    int start_index = 0;
    Value *keyword_start = NULL;
    Value *keyword_current = arg_start;
    for (int i = 0; i < arg_count; i++) {
      if (keyword_start == NULL && IS_KEYWORD(*keyword_current)) {
        keyword_start = keyword_current;
        start_index = i;
      }

      if (keyword_start != NULL) {
        // Copy the value to temporary storage
        // TODO: Error if we've reached the storage max
        stored_keyword_args[i - start_index] = *keyword_current;
      }

      keyword_current++;
    }

    // Reset the top of the stack to the location of the first keyword argument
    vm->stack_top = keyword_start;

    // Now that we know where keywords start in the value stack, push the
    // keyword default values on so that they line up with the local variables
    // we've defined
    KeywordArgument *keyword_arg = closure->function->keyword_args.args;
    for (int i = 0; i < closure->function->keyword_args.count; i++) {
      // Check if the passed keyword args match this keyword
      bool found_match = false;
      for (int j = 0; j < arg_count - start_index; j += 2) {
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
  } else {
    if (arg_count != closure->function->arity) {
      vm_runtime_error(vm, "Expected %d arguments but got %d.", closure->function->arity,
                       arg_count);
      return false;
    }
  }

  CallFrame *frame = &vm->frames[vm->frame_count++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  frame->slots = arg_start - 1;
  return true;
}

static bool vm_call_value(VM *vm, Value callee, uint8_t arg_count, uint8_t keyword_count) {
  if (IS_OBJECT(callee)) {
    switch (OBJECT_KIND(callee)) {
    case ObjectKindClosure:
      return vm_call(vm, AS_CLOSURE(callee), arg_count);
    case ObjectKindNativeFunction: {
      FunctionPtr func_ptr = AS_NATIVE_FUNC(callee);
      Value result = func_ptr((MescheMemory *)vm, arg_count, vm->stack_top - arg_count);

      // Pop off all of the argument and the function itself
      for (int i = 0; i < arg_count + 1; i++) {
        mesche_vm_stack_pop(vm);
      }

      // Push the result to store it
      mesche_vm_stack_push(vm, result);
      return true;
    }
    case ObjectKindRecord: {
      ObjectRecord *record_type = AS_RECORD_TYPE(callee);
      ObjectRecordInstance *instance = mesche_object_make_record_instance(vm, record_type);

      // Initialize the value array using keyword values or the default for each field
      for (int i = 0; i < record_type->fields.count; i++) {
        bool found_value = false;
        ObjectRecordField *record_field = AS_RECORD_FIELD(record_type->fields.values[i]);

        // Look for the field's value in the keyword parameters
        for (int i = (keyword_count * 2) - 1; i >= 0; i -= 2) {
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

      // Pop the record and key/value pairs off the stack
      for (int i = 0; i < (keyword_count * 2) + 1; i++) {
        mesche_vm_stack_pop(vm);
      }

      // Push the record instance in its place
      mesche_vm_stack_push(vm, OBJECT_VAL(instance));
      return true;
    };
    case ObjectKindRecordFieldAccessor: {
      ObjectRecordFieldAccessor *accessor = AS_RECORD_FIELD_ACCESSOR(callee);
      ObjectRecordInstance *instance = AS_RECORD_INSTANCE(mesche_vm_stack_pop(vm));

      // TODO: Be somewhat tolerant to record type version?
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
    default:
      break; // Value not callable
    }
  }

  vm_runtime_error(vm, "Only functions can be called (received kind %d)", OBJECT_KIND(callee));

  return false;
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
    printf(" ");
    for (Value *slot = vm->stack; slot < vm->stack_top; slot++) {
      printf("[ ");
      mesche_value_print(*slot);
      printf(" ]");
    }
    printf("\n");

    mesche_disasm_instr(&frame->closure->function->chunk,
                        (int)(frame->ip - frame->closure->function->chunk.code));
#endif

    uint8_t instr;
    uint8_t offset;
    Value value;
    ObjectString *name;
    uint8_t slot = 0;

    switch (instr = READ_BYTE()) {
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
      Value cdr = mesche_vm_stack_pop(vm);
      Value car = mesche_vm_stack_pop(vm);
      mesche_vm_stack_push(vm, OBJECT_VAL(mesche_object_make_cons(vm, car, cdr)));
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
        for (int i = 0; i < item_count; i++) {
          list_value = mesche_vm_stack_pop(vm);
          list =
              mesche_object_make_cons(vm, list_value, list == NULL ? EMPTY_VAL : OBJECT_VAL(list));
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
    case OP_AND:
      BINARY_OP(BOOL_VAL, IS_ANY, AS_BOOL, &&);
      break;
    case OP_OR:
      BINARY_OP(BOOL_VAL, IS_ANY, AS_BOOL, ||);
      break;
    case OP_NOT:
      mesche_vm_stack_push(vm, IS_NIL(mesche_vm_stack_pop(vm)) ? T_VAL : NIL_VAL);
      break;
    case OP_EQUAL:
      // Drop through for now
    case OP_EQV: {
      Value b = mesche_vm_stack_pop(vm);
      Value a = mesche_vm_stack_pop(vm);
      mesche_vm_stack_push(vm, BOOL_VAL(mesche_value_equalp(a, b)));
      break;
    }
    case OP_JUMP:
      offset = READ_SHORT();
      frame->ip += offset;
      break;
    case OP_JUMP_IF_FALSE:
      offset = READ_SHORT();
      if (IS_FALSEY(vm_stack_peek(vm, 0))) {
        frame->ip += offset;
      }
      break;
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


      // TODO: Consider adding this back if we figure out the right approach.
      // It will allow us to clear out any intermediate stack values on return.
      /* vm->stack_top = frame->slots; */

      // Restore the previous result value, call frame, and value stack pointer
      // before continuing execution
      mesche_vm_stack_pop(vm); // Pop the closure before restoring result
      mesche_vm_stack_push(vm, value);
      frame = &vm->frames[vm->frame_count - 1];
      break;
    case OP_DISPLAY:
      // Peek at the value on the stack
      mesche_value_print(vm_stack_peek(vm, 0));
      break;
    case OP_LOAD_FILE: {
      ObjectString *path = READ_STRING();
      mesche_vm_load_file(vm, path->chars);

      // Execute the file's closure
      Value *stack_top = vm->stack_top;
      if (vm->stack_top >= stack_top && IS_CLOSURE(vm_stack_peek(vm, 0))) {
        ObjectClosure *closure = AS_CLOSURE(vm_stack_peek(vm, 0));
        if (closure->function->type == TYPE_SCRIPT) {
          // Call the script
          vm_call(vm, closure, 0);
          frame = &vm->frames[vm->frame_count - 1];
        }
      }

      // Pop the file path and closure off the stack
      mesche_vm_stack_pop(vm);
      mesche_vm_stack_pop(vm);

      break;
    }
    case OP_DEFINE_RECORD: {
      // Skip all the fields to find the name
      uint8_t field_count = READ_BYTE();
      ObjectSymbol *record_name = AS_SYMBOL(vm_stack_peek(vm, field_count * 2));
      ObjectRecord *record = mesche_object_make_record(vm, &record_name->string);
      Table *module_scope = &vm->current_module->locals;

      // Create a binding for the maker function
      char *maker_name = mesche_cstring_join("make-", 5, record_name->string.chars,
                                             record_name->string.length, "");
      ObjectString *maker_name_string =
          mesche_object_make_string(vm, maker_name, strlen(maker_name));
      vm_create_module_binding(vm, CURRENT_MODULE(), maker_name_string, OBJECT_VAL(record), true);
      free(maker_name);

      for (int i = 0; i < field_count; i++) {
        // Build the record field from name and default value
        int stack_pos = ((field_count - i) * 2) - 1;
        ObjectSymbol *name = AS_SYMBOL(vm_stack_peek(vm, stack_pos));
        Value value = vm_stack_peek(vm, stack_pos - 1);
        ObjectRecordField *field = mesche_object_make_record_field(vm, &name->string, value);
        mesche_value_array_write((MescheMemory *)vm, &record->fields, OBJECT_VAL(field));

        // Create a binding for the field accessor "function"
        char *accessor_name =
            mesche_cstring_join(record_name->string.chars, record_name->string.length,
                                name->string.chars, name->string.length, "-");
        ObjectString *accessor_name_string =
            mesche_object_make_string(vm, accessor_name, strlen(accessor_name));
        free(accessor_name);
        ObjectRecordFieldAccessor *accessor = mesche_object_make_record_accessor(vm, record, i);
        vm_create_module_binding(vm, CURRENT_MODULE(), accessor_name_string, OBJECT_VAL(accessor),
                                 true);
      }

      // Pop all of the fields and record name off of the stack
      for (int i = 0; i < (field_count * 2) + 1; i++) {
        mesche_vm_stack_pop(vm);
      }

      // Push the record type onto the stack
      mesche_vm_stack_push(vm, OBJECT_VAL(record));

      break;
    }
    case OP_DEFINE_MODULE: {
      // Resolve the module and set the current closure's module.
      // This works because scripts are compiled into functions with
      // their own closure, so a `define-module` will cause that to
      // be set.
      ObjectCons *list = AS_CONS(mesche_vm_stack_pop(vm));
      // TODO: Error if module is already set?
      frame->closure->module = mesche_module_resolve_by_path(vm, list);

      // Push the defined module onto the stack
      mesche_vm_stack_push(vm, OBJECT_VAL(frame->closure->module));
      break;
    }
    case OP_RESOLVE_MODULE: {
      // Resolve the module based on the given path
      ObjectCons *list = AS_CONS(mesche_vm_stack_pop(vm));
      ObjectModule *resolved_module = mesche_module_resolve_by_path(vm, list);

      // Compiling the module file will push its closure onto the stack
      if (IS_CLOSURE(vm_stack_peek(vm, 0))) {
        ObjectClosure *closure = AS_CLOSURE(mesche_vm_stack_pop(vm));
        if (closure->function->type == TYPE_SCRIPT) {
          // Set up the module's closure for execution in the next VM loop
          // TODO: This pattern needs to be made into a macro!
          vm_call(vm, closure, 0);
          frame = &vm->frames[vm->frame_count - 1];
        }

        // Push the module onto the stack before pushing the closure back.  We
        // leave the closure on the stack so that OP_RETURN semantics are not
        // affected!
        mesche_vm_stack_push(vm, OBJECT_VAL(resolved_module));
        mesche_vm_stack_push(vm, OBJECT_VAL(closure));
      } else {
        // This case will happen when the module has previously been executed
        // and was merely resolved this time.  We have to emulate the load of
        // a module to keep the stack handling consistent, so push the resolved
        // module and a nil val so that `OP_IMPORT_MODULE` doesn't have to the
        // stack first!
        mesche_vm_stack_push(vm, OBJECT_VAL(resolved_module));
        mesche_vm_stack_push(vm, NIL_VAL);
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

      // Nothing new is pushed onto the stack
      break;
    }
    case OP_ENTER_MODULE: {
      ObjectCons *list = AS_CONS(mesche_vm_stack_pop(vm));
      ObjectModule *module = mesche_module_resolve_by_path(vm, list);
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
      Table *globals = &CURRENT_MODULE()->locals;
      if (!mesche_table_get(globals, name, &value)) {
        vm_runtime_error(vm, "Undefined variable '%s'.", name->chars);
        return INTERPRET_RUNTIME_ERROR;
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
                         keyword_count)) {
        return INTERPRET_COMPILE_ERROR;
      }

      // Set the current frame to the new call frame
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
    case OP_CLOSE_UPVALUE:
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

void mesche_vm_load_path_add(VM *vm, const char *load_path) {
  char *resolved_path = mesche_fs_resolve_path(load_path);
  if (resolved_path) {
    ObjectString *path_str =
        mesche_object_make_string(vm, resolved_path, strlen(resolved_path));
    vm->load_paths = mesche_list_push(vm, vm->load_paths, OBJECT_VAL(path_str));
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
    vm_call(vm, closure, 0);
    return mesche_vm_run(vm);
  }
}

InterpretResult mesche_vm_load_module(VM *vm, const char *module_path) {
  char *source = mesche_fs_file_read_all(module_path);
  if (source == NULL) {
    // TODO: Report and fail gracefully
    PANIC("ERROR: Could not load script file: %s\n\n", module_path);
  }

  ObjectFunction *function = mesche_compile_source(vm, source);
  if (function == NULL) {
    return INTERPRET_COMPILE_ERROR;
  }

  // Push the top-level function as a closure but do not execute it yet!
  // The OP_IMPORT_MODULE instruction will check for a new script closure
  // at the top of the value stack and will execute it if found.
  mesche_vm_stack_push(vm, OBJECT_VAL(function));
  ObjectClosure *closure = mesche_object_make_closure(vm, function, NULL);
  mesche_vm_stack_pop(vm);
  mesche_vm_stack_push(vm, OBJECT_VAL(closure));

  if (!vm->is_running) {
    // Call the initial closure and run the VM
    vm_call(vm, closure, 0);
    return mesche_vm_run(vm);
  }
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

  return result;
}
