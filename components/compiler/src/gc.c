#include "array.h"
#include "compiler.h"
#include "continuation.h"
#include "native.h"
#include "object.h"
#include "record.h"
#include "util.h"
#include "vm-impl.h"

void mesche_gc_mark_object(VM *vm, Object *object) {
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
  if ((object->kind != ObjectKindString && object->kind != ObjectKindKeyword &&
       object->kind != ObjectKindNativeFunction && object->kind != ObjectKindPointer &&
       object->kind != ObjectKindStackMarker) ||
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

static void gc_mark_value(VM *vm, Value value) {
  if (IS_OBJECT(value))
    mesche_gc_mark_object(vm, AS_OBJECT(value));
}

static void gc_mark_array(VM *vm, ValueArray *array) {
  for (int i = 0; i < array->count; i++) {
    gc_mark_value(vm, array->values[i]);
  }
}

static void gc_mark_table(VM *vm, Table *table) {
  for (int i = 0; i < table->capacity; i++) {
    Entry *entry = &table->entries[i];
    mesche_gc_mark_object(vm, (Object *)entry->key);
    gc_mark_value(vm, entry->value);
  }
}

static void gc_mark_roots(void *target) {
  VM *vm = (VM *)target;
  for (Value *slot = vm->stack; slot < vm->stack_top; slot++) {
    gc_mark_value(vm, *slot);
  }

  for (int i = 0; i < vm->frame_count; i++) {
    mesche_gc_mark_object(vm, (Object *)vm->frames[i].closure);
  }

  for (ObjectUpvalue *upvalue = vm->open_upvalues; upvalue != NULL; upvalue = upvalue->next) {
    mesche_gc_mark_object(vm, (Object *)upvalue);
  }

  // Mark standard I/O ports
  if (vm->input_port) {
    mesche_gc_mark_object(vm, (Object *)vm->input_port);
  }
  if (vm->output_port) {
    mesche_gc_mark_object(vm, (Object *)vm->output_port);
  }
  if (vm->error_port) {
    mesche_gc_mark_object(vm, (Object *)vm->error_port);
  }

  // Mark reusable symbols
  if (vm->quote_symbol) {
    mesche_gc_mark_object(vm, (Object *)vm->quote_symbol);
  }

  // Mark the current reset stack marker
  if (vm->current_reset_marker) {
    mesche_gc_mark_object(vm, (Object *)vm->current_reset_marker);
  }

  // Mark the load path list
  if (vm->load_paths) {
    gc_mark_value(vm, OBJECT_VAL(vm->load_paths));
  }

  // Mark roots in every module
  gc_mark_table(vm, &vm->modules);
}

static void gc_darken_object(VM *vm, Object *object) {
#ifdef DEBUG_LOG_GC
  printf("%p    darken ", (void *)object);
  mesche_value_print(OBJECT_VAL(object));
  printf("\n");
#endif

  switch (object->kind) {
  case ObjectKindCons: {
    ObjectCons *cons = (ObjectCons *)object;
    gc_mark_value(vm, cons->car);
    gc_mark_value(vm, cons->cdr);
    break;
  }
  case ObjectKindSymbol: {
    ObjectSymbol *symbol = (ObjectSymbol *)object;
    mesche_gc_mark_object(vm, (Object *)symbol->name);
    break;
  }
  case ObjectKindSyntax: {
    ObjectSyntax *syntax = (ObjectSyntax *)object;
    gc_mark_value(vm, syntax->value);
    mesche_gc_mark_object(vm, (Object *)syntax->file_name);
    break;
  }
  case ObjectKindArray: {
    ObjectArray *array = (ObjectArray *)object;
    gc_mark_array(vm, &array->objects);
    break;
  }
  case ObjectKindClosure: {
    ObjectClosure *closure = (ObjectClosure *)object;
    mesche_gc_mark_object(vm, (Object *)closure->function);
    for (int i = 0; i < closure->upvalue_count; i++) {
      mesche_gc_mark_object(vm, (Object *)closure->upvalues[i]);
    }
    break;
  }
  case ObjectKindContinuation: {
    ObjectContinuation *continuation = (ObjectContinuation *)object;

    // We could be marking the continuation before it's fully initialized, so be
    // careful
    if (continuation->frame_count > 0) {
      for (int i = 0; i < continuation->frame_count; i++) {
        mesche_gc_mark_object(vm, (Object *)continuation->frames[i].closure);
      }

      for (int i = 0; i < continuation->stack_count; i++) {
        gc_mark_value(vm, continuation->stack[i]);
      }
    }

    break;
  }
  case ObjectKindFunction: {
    ObjectFunction *function = (ObjectFunction *)object;
    mesche_gc_mark_object(vm, (Object *)function->name);
    gc_mark_array(vm, &function->chunk.constants);

    // Mark strings associated with keyword arguments
    for (int i = 0; i < function->keyword_args.count; i++) {
      mesche_gc_mark_object(vm, (Object *)function->keyword_args.args[i].name);
    }
    break;
  }
  case ObjectKindPointer: {
    ObjectPointer *pointer = (ObjectPointer *)object;
    if (pointer->type != NULL && pointer->type->mark_func != NULL) {
      pointer->type->mark_func((MescheMemory *)vm, pointer->ptr);
    }
    break;
  }
  case ObjectKindUpvalue:
    gc_mark_value(vm, ((ObjectUpvalue *)object)->closed);
    break;
  case ObjectKindModule: {
    ObjectModule *module = (ObjectModule *)object;
    mesche_gc_mark_object(vm, (Object *)module->name);
    gc_mark_table(vm, &module->locals);
    gc_mark_array(vm, &module->imports);
    gc_mark_array(vm, &module->exports);
    mesche_gc_mark_object(vm, (Object *)module->init_function);
    break;
  }
  case ObjectKindRecord: {
    ObjectRecord *record = (ObjectRecord *)object;
    mesche_gc_mark_object(vm, (Object *)record->name);
    gc_mark_array(vm, &record->fields);
    break;
  }
  case ObjectKindRecordField: {
    ObjectRecordField *field = (ObjectRecordField *)object;
    mesche_gc_mark_object(vm, (Object *)field->name);
    gc_mark_value(vm, field->default_value);
    break;
  }
  case ObjectKindRecordFieldAccessor: {
    ObjectRecordFieldAccessor *accessor = (ObjectRecordFieldAccessor *)object;
    mesche_gc_mark_object(vm, (Object *)accessor->record_type);
    break;
  }
  case ObjectKindRecordFieldSetter: {
    ObjectRecordFieldSetter *setter = (ObjectRecordFieldSetter *)object;
    mesche_gc_mark_object(vm, (Object *)setter->record_type);
    break;
  }
  case ObjectKindRecordInstance: {
    ObjectRecordInstance *instance = (ObjectRecordInstance *)object;
    gc_mark_array(vm, &instance->field_values);
    mesche_gc_mark_object(vm, (Object *)instance->record_type);
    break;
  }
  default:
    break;
  }
}

static void gc_trace_references(MescheMemory *mem) {
  VM *vm = (VM *)mem;

  // Loop through the stack (which may get more entries added during the loop)
  // to darken all marked objects
  while (vm->gray_count > 0) {
    Object *object = vm->gray_stack[--vm->gray_count];
    gc_darken_object(vm, object);
  }
}

static void gc_table_remove_white(Table *table) {
  for (int i = 0; i < table->capacity; i++) {
    Entry *entry = &table->entries[i];
    if (entry->key != NULL && !entry->key->object.is_marked) {
      mesche_table_delete(table, entry->key);
    }
  }
}

static void gc_sweep_objects(VM *vm) {
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

void mesche_gc_collect_garbage(MescheMemory *mem) {
  VM *vm = (VM *)mem;
  gc_mark_roots(vm);
  if (vm->current_compiler != NULL) {
    mesche_compiler_mark_roots(vm->current_compiler);
  }
  gc_trace_references((MescheMemory *)vm);
  gc_table_remove_white(&vm->strings);
  gc_table_remove_white(&vm->symbols);
  gc_table_remove_white(&vm->keywords);
  gc_sweep_objects(vm);
}
