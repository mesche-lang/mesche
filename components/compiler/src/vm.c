#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "array.h"
#include "chunk.h"
#include "compiler.h"
#include "continuation.h"
#include "core.h"
#include "disasm.h"
#include "error.h"
#include "fs.h"
#include "gc.h"
#include "io.h"
#include "keyword.h"
#include "list.h"
#include "math.h"
#include "mem.h"
#include "module.h"
#include "native.h"
#include "object.h"
#include "op.h"
#include "port.h"
#include "process.h"
#include "record.h"
#include "string.h"
#include "syntax.h"
#include "time.h"
#include "util.h"
#include "value.h"
#include "vm-impl.h"

// Predeclare module init functions
void mesche_io_module_init(VM *vm);

// NOTE: Enable this for diagnostic purposes
/* #define DEBUG_TRACE_EXECUTION */

#define PRINT_VALUE_STACK()                                                                        \
  printf("\nValue Stack:\n");                                                                      \
  for (Value *slot = vm->stack; slot < vm->stack_top; slot++) {                                    \
    printf("  %3d: ", abs(slot - vm->stack_top));                                                  \
    mesche_value_print(vm->output_port, *slot);                                                    \
    printf("\n");                                                                                  \
  }                                                                                                \
  printf("\n");

#define PRINT_CALL_STACK()                                                                         \
  printf("\nCall Stack:\n");                                                                       \
  for (int i = 0; i < vm->frame_count; i++) {                                                      \
    printf("  %3d: ", i);                                                                          \
    mesche_value_print(vm->output_port, OBJECT_VAL(vm->frames[i].closure));                        \
    if (i == vm->current_reset_marker->frame_index) {                                              \
      printf(" [reset]");                                                                          \
    }                                                                                              \
    printf("\n");                                                                                  \
  }

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

// TODO: This should set a field on VM object which gets read after each function call (?)

void mesche_vm_raise_error(VM *vm, const char *format, ...) {
  CallFrame *frame = &vm->frames[vm->frame_count - 1];

  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  size_t instruction = frame->ip - frame->closure->function->chunk.code - 1;
  int line = frame->closure->function->chunk.lines[instruction];
  ObjectString *file_name = frame->closure->function->chunk.file_name;
  fprintf(stderr, "[line %d] in %s\n", line, file_name ? file_name->chars : "script");

  PRINT_CALL_STACK();

  // TODO: Start debugger if necessary
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

void mesche_vm_register_core_modules(VM *vm, char *module_path) {
  // Add the module path first so that native functions are loaded in that context
  mesche_vm_load_path_add(vm, module_path);

  // Load the core module first because it serves as the "global" scope
  mesche_core_module_init(vm);
  if (mesche_vm_eval_string(vm, "(module-import (mesche core))") != INTERPRET_OK) {
    PANIC("ERROR: Could not load `mesche core` from module path: %s\n\n", module_path);
  }

  // Store the core module before initializing other core modules
  Value core_module = mesche_vm_stack_pop(vm);
  vm->core_module = AS_MODULE(core_module);
  if (!IS_MODULE(core_module)) {
    PANIC("ERROR: Unexpected problem loading `mesche core` from module path: %s\n\n", module_path);
  }

  // Then, load the expander source and store the `expand` function
  /* if (mesche_vm_eval_string(vm, "(module-import (mesche expander)) expand") != INTERPRET_OK) { */
  /*   PANIC("ERROR: Could not load `mesche expander` from module path: %s\n\n", module_path); */
  /* } */

  /* Value expander = mesche_vm_stack_pop(vm); */
  /* vm->expander = AS_CLOSURE(expander); */
  /* if (!IS_CLOSURE(expander)) { */
  /*   PANIC("ERROR: Unexpected problem loading `mesche expander` from module path: %s\n\n", */
  /*         module_path); */
  /* } */

  mesche_io_module_init(vm);
  mesche_fs_module_init(vm);
  mesche_list_module_init(vm);
  mesche_math_module_init(vm);
  mesche_time_module_init(vm);
  mesche_array_module_init(vm);
  mesche_string_module_init(vm);
  mesche_reader_module_init(vm);
  mesche_module_module_init(vm);
  mesche_compiler_module_init(vm);
  mesche_process_module_init(vm);
}

void mesche_vm_init(VM *vm, int arg_count, char **arg_array) {
  // Initialize the memory manager
  mesche_mem_init(&vm->mem, mesche_gc_collect_garbage);

  // Initialize the gray stack before allocating anything
  vm->gray_count = 0;
  vm->gray_capacity = 0;
  vm->gray_stack = NULL;

  vm->is_running = false;
  vm->objects = NULL;
  vm->load_paths = NULL;
  vm->current_compiler = NULL;
  vm->core_module = NULL;
  vm->expander = NULL;
  vm->quote_symbol = NULL;
  vm->current_reset_marker = NULL;
  vm->open_upvalues = NULL;
  vm->input_port = NULL;
  vm->output_port = NULL;
  vm->error_port = NULL;
  vm->stack_top = vm->stack;

  // Initialize the interned string, symbol, and keyword tables
  mesche_table_init(&vm->strings);
  mesche_table_init(&vm->symbols);
  mesche_table_init(&vm->keywords);
  mesche_table_init(&vm->modules);

  // Set up ports for standard file descriptors
  vm->input_port = AS_PORT(mesche_io_make_file_port(vm, MeschePortKindInput, stdin, "stdin", 0));
  vm->input_port->can_close = false;
  vm->output_port =
      AS_PORT(mesche_io_make_file_port(vm, MeschePortKindOutput, stdout, "stdout", 0));
  vm->output_port->can_close = false;
  vm->error_port = AS_PORT(mesche_io_make_file_port(vm, MeschePortKindOutput, stderr, "stderr", 0));
  vm->error_port->can_close = false;

  // Initialize reusable symbols
  vm->quote_symbol = mesche_object_make_symbol(vm, "quote", 5);
  vm->quote_symbol->token_kind = TokenKindQuote;

  // Initialize the module table root module
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

  // Reset the stack
  vm_reset_stack(vm);
}

void mesche_vm_free(VM *vm) {
  // Reset stacks to lose references to things allocated there
  vm_reset_stack(vm);
  vm->open_upvalues = NULL;

  // Release handles to standard I/O ports
  vm->input_port = NULL;
  vm->output_port = NULL;
  vm->error_port = NULL;

  // Do one final GC pass
  mesche_mem_collect_garbage((MescheMemory *)vm);

  // Free remaining roots
  mesche_table_free((MescheMemory *)vm, &vm->modules);
  mesche_table_free((MescheMemory *)vm, &vm->symbols);
  mesche_table_free((MescheMemory *)vm, &vm->strings);
  mesche_table_free((MescheMemory *)vm, &vm->keywords);
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
    mesche_vm_raise_error(vm, "Expected %d arguments but got %d.", closure->function->arity,
                          arg_count);
    return false;
  } else if (closure->function->rest_arg_index > 0 && arg_count < closure->function->arity - 1) {
    mesche_vm_raise_error(vm, "Expected at least %d arguments but got %d.",
                          closure->function->arity - 1, arg_count);
    return false;
  }

  // Locate the first argument on the value stack
  Value *arg_start = vm->stack_top - (arg_count + (keyword_count * 2));

  // Store the number of keyword arguments the function takes, we'll need it later
  int num_keyword_args = closure->function->keyword_args.count;

  // Process keyword arguments, if any
  if (keyword_count > 0) {
    if (num_keyword_args == 0) {
      mesche_vm_raise_error(vm, "Function does not accept keyword arguments.");
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
        // If no default value was provided, choose `#f`
        mesche_vm_stack_push(vm, FALSE_VAL);
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
        // If no default value was provided, choose `#f`
        mesche_vm_stack_push(vm, FALSE_VAL);
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
      // Fill in rest arg with #f if nothing was passed for it
      if (num_keyword_args > 0) {
        // Shift the keyword args forward by 1
        memmove(arg_start + closure->function->arity + 1, arg_start + closure->function->arity,
                sizeof(Value) * num_keyword_args);
      }

      // Place the #f value and update the stack top
      *(vm->stack_top - num_keyword_args) = FALSE_VAL;
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
    case ObjectKindFunction: {
      // We're likely invoking a script function from the REPL so wrap it in a closure an execute it
      ObjectClosure *closure =
          mesche_object_make_closure(vm, AS_FUNCTION(callee), vm->current_module);
      mesche_vm_stack_push(vm, OBJECT_VAL(closure));

      // Invoke the new closure as a normal call, not a tail call
      return vm_call(vm, closure, arg_count, keyword_count, false);
    }
    case ObjectKindClosure:
      return vm_call(vm, AS_CLOSURE(callee), arg_count, keyword_count, is_tail_call);
    case ObjectKindNativeFunction: {
      FunctionPtr func_ptr = AS_NATIVE_FUNC(callee);
      int total_args = arg_count + keyword_count * 2;
      // TODO: Need to push the native function on the stack somehow
      Value result = func_ptr(vm, total_args, vm->stack_top - total_args);

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

      // Initialize the value array using keyword values or the default for each field
      for (int i = 0; i < record_type->fields.count; i++) {
        bool found_value = false;
        ObjectRecordField *record_field = AS_RECORD_FIELD(record_type->fields.values[i]);

        // Look for the field's value in the arguments
        for (int i = arg_count; i >= 0; i -= 2) {
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
      for (int i = 0; i < arg_count + 1; i++) {
        mesche_vm_stack_pop(vm);
      }

      // Push the record instance back on to the stack as the result
      mesche_vm_stack_push(vm, OBJECT_VAL(instance));
      return true;
    };
    case ObjectKindRecordFieldAccessor: {
      ObjectRecordFieldAccessor *accessor = AS_RECORD_FIELD_ACCESSOR(callee);

      if (arg_count != 1) {
        mesche_vm_raise_error(
            vm, "Record field accessor for type '%s' requires a single record instance argument.",
            accessor->record_type->name->chars);
        return false;
      }

      Value possible_instance = vm_stack_peek(vm, 0);
      if (!IS_OBJECT(possible_instance)) {
        mesche_vm_raise_error(
            vm, "Expected instance of record type %s but received non-object kind %d.",
            accessor->record_type->name->chars, possible_instance.kind);
        return false;
      }

      if (!IS_RECORD_INSTANCE(possible_instance)) {
        mesche_vm_raise_error(
            vm, "Expected instance of record type %s but received object kind %d.",
            accessor->record_type->name->chars, AS_OBJECT(possible_instance)->kind);
        return false;
      }

      // TODO: Be somewhat tolerant to record type version?
      ObjectRecordInstance *instance = AS_RECORD_INSTANCE(possible_instance);
      if (instance->record_type != accessor->record_type) {
        mesche_vm_raise_error(vm, "Passed record of type %s to accessor that expects %s.",
                              instance->record_type->name->chars,
                              accessor->record_type->name->chars);
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
        mesche_vm_raise_error(
            vm,
            "Record field setter for type '%s' requires a record instance argument "
            "and a value to set for the associated field.",
            setter->record_type->name->chars);
        return false;
      }

      Value possible_instance = vm_stack_peek(vm, 1);
      if (!IS_OBJECT(possible_instance)) {
        mesche_vm_raise_error(
            vm, "Expected instance of record type %s but received non-object kind %d.",
            setter->record_type->name->chars, possible_instance.kind);
        return false;
      }

      if (!IS_RECORD_INSTANCE(possible_instance)) {
        mesche_vm_raise_error(vm,
                              "Expected instance of record type %s but received object kind %d.",
                              setter->record_type->name->chars, AS_OBJECT(possible_instance)->kind);
        return false;
      }

      // TODO: Be somewhat tolerant to record type version?
      ObjectRecordInstance *instance = AS_RECORD_INSTANCE(possible_instance);
      if (instance->record_type != setter->record_type) {
        mesche_vm_raise_error(vm, "Passed record of type %s to setter that expects %s.",
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
    case ObjectKindRecordPredicate: {
      ObjectRecordPredicate *predicate = AS_RECORD_PREDICATE(callee);

      if (arg_count != 1) {
        mesche_vm_raise_error(vm,
                              "Record type predicate '%s?' requires a record instance argument.",
                              predicate->record_type->name->chars);
        return false;
      }

      Value arg = vm_stack_peek(vm, 0);

      // Check if the parameter is a record instance and if its type matches the
      // type of the predicate
      mesche_vm_stack_push(vm, IS_RECORD_INSTANCE(arg) && (AS_RECORD_INSTANCE(arg)->record_type ==
                                                           predicate->record_type)
                                   ? TRUE_VAL
                                   : FALSE_VAL);
      return true;
    }
    default:
      break; // Value not callable
    }
  }

  mesche_vm_raise_error(vm, "Only functions can be called (received kind %d)", OBJECT_KIND(callee));

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
  int entry_frame = vm->frame_count - 1;
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
      mesche_vm_raise_error(vm, "Operands must be numbers.");                                      \
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
        mesche_value_print(vm->output_port, *slot);
        printf("\n");
      }
      printf("\n");

      printf("Call Stack:\n");
      for (int i = 0; i < vm->frame_count; i++) {
        printf("  %3d: ", i);
        mesche_value_print(vm->output_port, OBJECT_VAL(vm->frames[i].closure));
        if (i == vm->current_reset_marker->frame_index) {
          printf(" [reset]");
        }
        printf("\n");
      }

      printf("\n");

      printf("Executing:\n");
      mesche_disasm_instr(vm->output_port, &frame->closure->function->chunk,
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
    case OP_FALSE:
      mesche_vm_stack_push(vm, FALSE_VAL);
      break;
    case OP_TRUE:
      mesche_vm_stack_push(vm, TRUE_VAL);
      break;
    case OP_EMPTY:
      mesche_vm_stack_push(vm, EMPTY_VAL);
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
        mesche_vm_raise_error(vm, "Operands must be numbers.");
        return INTERPRET_RUNTIME_ERROR;
      }
      int b = (int)AS_NUMBER(mesche_vm_stack_pop(vm));
      int a = (int)AS_NUMBER(mesche_vm_stack_pop(vm));
      mesche_vm_stack_push(vm, NUMBER_VAL(a % b));
      break;
    }
    case OP_NOT:
      mesche_vm_stack_push(vm, IS_FALSE(mesche_vm_stack_pop(vm)) ? TRUE_VAL : FALSE_VAL);
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
      mesche_vm_stack_push(vm, BOOL_VAL(mesche_value_eqv_p(a, b)));
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

      // Return the stack back to the previous frame by popping the arguments
      // and closure off the stack
      vm->stack_top -= frame->total_arg_count;
      vm->frame_count--;

      // If we've returned to the frame index where we entered vm_run(), exit execution
      if (vm->frame_count == entry_frame) {
        // The VM is no longer executing if we've exhausted the call frames
        if (vm->frame_count == 0) {
          vm->is_running = false;
        }

        // Pop the entry function off of the stack
        mesche_vm_stack_pop(vm);

        // Push the return value back on so that it can be read by the REPL
        mesche_vm_stack_push(vm, value);

        return INTERPRET_OK;
      }

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
    case OP_BREAK:
      // Print out diagnostics and end execution
      PRINT_VALUE_STACK();

      printf("Call Stack:\n");
      for (int i = 0; i < vm->frame_count; i++) {
        printf("  %3d: ", i);
        mesche_value_print(vm->output_port, OBJECT_VAL(vm->frames[i].closure));
        if (i == vm->current_reset_marker->frame_index) {
          printf(" [reset]");
        }
        printf("\n");
      }

      printf("\n");

      printf("Current Function:\n");
      mesche_disasm_function(vm->output_port, frame->closure->function);
      printf("\n");

      mesche_vm_raise_error(vm, "Exiting due to `break`.");
      return INTERPRET_RUNTIME_ERROR;
    case OP_DISPLAY:
      // Peek at the value on the stack
      mesche_value_print(vm->output_port, mesche_vm_stack_pop(vm));
      mesche_vm_stack_push(vm, UNSPECIFIED_VAL);
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
          mesche_object_make_string(vm, name_symbol->name->chars, name_symbol->name->length);
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

      // Create a binding for the predicate function
      char *predicate_name =
          mesche_cstring_join(record_name->chars, record_name->length, "?", 1, "");
      ObjectString *predicate_name_string =
          mesche_object_make_string(vm, predicate_name, record_name->length + 1);
      mesche_vm_stack_push(vm, OBJECT_VAL(predicate_name_string));
      ObjectRecordPredicate *predicate = mesche_object_make_record_predicate(vm, record);
      mesche_vm_stack_push(vm, OBJECT_VAL(predicate));
      free(predicate_name);

      // Create the binding for the maker and then pop off the record type and name string
      vm_create_module_binding(vm, CURRENT_MODULE(), maker_name_string, OBJECT_VAL(record), true);
      vm_create_module_binding(vm, CURRENT_MODULE(), predicate_name_string, OBJECT_VAL(predicate),
                               true);
      mesche_vm_stack_pop(vm);
      mesche_vm_stack_pop(vm);
      mesche_vm_stack_pop(vm);
      mesche_vm_stack_pop(vm);
      mesche_vm_stack_pop(vm);

      for (int i = 0; i < field_count; i++) {
        // Build the record field from name and default value
        int stack_pos = ((field_count - i) * 2) - 1;
        ObjectSymbol *name = AS_SYMBOL(vm_stack_peek(vm, stack_pos));
        Value value = vm_stack_peek(vm, stack_pos + 1);
        ObjectRecordField *field = mesche_object_make_record_field(vm, name->name, value);
        mesche_vm_stack_push(vm, OBJECT_VAL(field));
        mesche_value_array_write((MescheMemory *)vm, &record->fields, OBJECT_VAL(field));
        mesche_vm_stack_pop(vm);

        // Create a binding for the field accessor "function"
        char *accessor_name = mesche_cstring_join(record_name->chars, record_name->length,
                                                  name->name->chars, name->name->length, "-");
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
      ObjectString *module_name = AS_STRING(vm_stack_peek(vm, 0));
      frame->closure->module = mesche_module_resolve_by_name(vm, module_name, true);

      // Pop the module name string from the stack
      mesche_vm_stack_pop(vm);

      // Push the defined module onto the stack
      mesche_vm_stack_push(vm, OBJECT_VAL(frame->closure->module));
      break;
    }
    case OP_IMPORT_MODULE: {
      // Resolve the module based on the given path
      ObjectString *module_name = AS_STRING(vm_stack_peek(vm, 0));
      ObjectModule *resolved_module = mesche_module_resolve_by_name(vm, module_name, true);

      // Pop the module name string from the stack
      mesche_vm_stack_pop(vm);

      // Pull in the module's exports
      mesche_module_import(vm, resolved_module, CURRENT_MODULE());

      // Push the resolved module to the stack
      mesche_vm_stack_push(vm, OBJECT_VAL(resolved_module));

      break;
    }
    case OP_ENTER_MODULE: {
      ObjectString *module_name = AS_STRING(mesche_vm_stack_pop(vm));
      ObjectModule *module = mesche_module_resolve_by_name(vm, module_name, true);
      // TODO: This might cause unexpected behavior!
      frame->closure->module = module;
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
        // Then, look up the variable in the core module's locals if it's loaded
        // by this point
        if (vm->core_module) {
          if (!mesche_table_get(&vm->core_module->locals, name, &value)) {
            mesche_vm_raise_error(vm, "Undefined variable '%s'.", name->chars);
            return INTERPRET_RUNTIME_ERROR;
          }
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
        mesche_vm_raise_error(vm, "Undefined variable '%s'.", name->chars);
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
        mesche_vm_raise_error(vm, "Cannot apply non-function value.");
        return INTERPRET_RUNTIME_ERROR;
      } else if (!IS_CONS(list_value) && !IS_EMPTY(list_value)) {
        mesche_vm_raise_error(vm, "Cannot apply function to non-list value.");
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
        mesche_value_print(vm->output_port, *slot);
        printf("\n");
      }
      printf("\n");

      printf("Call Stack After:\n");
      for (int i = 0; i < vm->frame_count; i++) {
        printf("  %3d: ", i);
        mesche_value_print(vm->output_port, OBJECT_VAL(vm->frames[i].closure));
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
  ObjectModule *module = module = mesche_module_resolve_by_name_string(vm, module_name, false);
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

InterpretResult mesche_vm_call_closure(VM *vm, ObjectClosure *closure, int arg_count, Value *args) {
  // Set up the closure for execution by pushing it and its arguments to the
  // stack
  mesche_vm_stack_push(vm, OBJECT_VAL(closure));
  for (int i = 0; i < arg_count; i++) {
    mesche_vm_stack_push(vm, args[i]);
  }

  // Call the initial closure and run the VM.  If the VM is already running,
  // `mesche_vm_run` will consider it a sub-prompt.
  vm_call(vm, closure, arg_count, 0, false);
  return mesche_vm_run(vm);
}

static InterpretResult vm_eval_internal(VM *vm, const char *script_string, const char *file_name) {
  // Create a string for the file name and store it in the stack temporarily
  ObjectString *file_name_str = NULL;
  if (file_name) {
    file_name_str = mesche_object_make_string(vm, file_name, strlen(file_name));
    mesche_vm_stack_push(vm, OBJECT_VAL(file_name_str));
  }

  // Create a port to read the file
  // TODO: This function should receive a port directly!
  MeschePort *port = AS_PORT(
      mesche_io_make_string_port(vm, MeschePortKindInput, script_string, strlen(script_string)));
  mesche_vm_stack_push(vm, OBJECT_VAL(port));

  // Create a new reader for this input and compile it
  Reader reader;
  mesche_reader_init(&reader, vm, port, file_name_str);
  Value compile_result = mesche_compile_source(vm, &reader);

  // Pop the port
  mesche_vm_stack_pop(vm);

  // The file name should have been used for syntaxes now so pop it from the stack
  if (file_name) {
    mesche_vm_stack_pop(vm);
  }

  // Check the compilation result
  ObjectFunction *function = NULL;
  if (IS_ERROR(compile_result)) {
    printf("Compiler error: %s\n", AS_ERROR(compile_result)->message->chars);
    return INTERPRET_COMPILE_ERROR;
  } else {
    function = AS_FUNCTION(compile_result);
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

InterpretResult mesche_vm_eval_string(VM *vm, const char *script_string) {
  vm_eval_internal(vm, script_string, NULL);
}

InterpretResult mesche_vm_load_module(VM *vm, ObjectModule *module, const char *module_path) {
  char *source = mesche_fs_file_read_all(module_path);
  if (source == NULL) {
    // TODO: Report and fail gracefully
    PANIC("ERROR: Could not load script file: %s\n\n", module_path);
  }

  // Create a string for the file name and store it in the stack temporarily
  ObjectString *module_path_str = mesche_object_make_string(vm, module_path, strlen(module_path));
  mesche_vm_stack_push(vm, OBJECT_VAL(module_path_str));

  // Create a new reader for this input
  Reader reader;
  MeschePort *port =
      AS_PORT(mesche_io_make_string_port(vm, MeschePortKindInput, source, strlen(source)));
  mesche_vm_stack_push(vm, OBJECT_VAL(port));
  mesche_reader_init(&reader, vm, port, module_path_str);

  // Compile the module source
  Value compile_result = mesche_compile_module(vm, module, &reader);

  // Free the module source
  mesche_vm_stack_pop(vm);
  free(source);

  module = NULL;
  if (IS_ERROR(compile_result)) {
    printf("Compiler error: %s\n", AS_ERROR(compile_result)->message->chars);
    return INTERPRET_COMPILE_ERROR;
  } else {
    module = AS_MODULE(compile_result);
  }

  ObjectClosure *closure = mesche_object_make_closure(vm, module->init_function, NULL);
  closure->module = module;

  // Invoke the module's closure immediately
  InterpretResult result = mesche_vm_call_closure(vm, closure, 0, NULL);
  if (result != INTERPRET_OK) {
    // TODO: Use a legitimate error
    PANIC("ERROR LOADING MODULE\n");
  }

  // Executing the module body will cause a value to be left on the stack, pop
  // it and then pass along the result value
  mesche_vm_stack_pop(vm);
  return result;
}

InterpretResult mesche_vm_load_file(VM *vm, const char *file_path) {
  char *source = mesche_fs_file_read_all(file_path);
  if (source == NULL) {
    // TODO: Report and fail gracefully
    PANIC("ERROR: Could not load script file: %s\n\n", file_path);
  }

  // Eval the script and then free the source string
  InterpretResult result = vm_eval_internal(vm, source, file_path);
  free(source);

  if (result == INTERPRET_COMPILE_ERROR) {
    mesche_vm_raise_error(vm, "Could not load file due to compilation error: %s\n", file_path);
    return result;
  }

  return result;
}
