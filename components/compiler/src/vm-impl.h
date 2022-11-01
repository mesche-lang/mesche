#ifndef mesche_vm_impl_h
#define mesche_vm_impl_h

#include <stdint.h>

#include "callframe.h"
#include "closure.h"
#include "module.h"
#include "object.h"
#include "reader.h"
#include "symbol.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define UINT8_COUNT (UINT8_MAX + 1)
#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct VM {
  // VM is a MescheMemory implementation
  // TODO: It possibly shouldn't be
  MescheMemory mem;

  // Runtime call and value stack tracking
  CallFrame frames[FRAMES_MAX];
  int frame_count;
  Value stack[STACK_MAX]; // TODO: Make this dynamically resizable
  Value *stack_top;
  Table strings;
  Table symbols;
  Table keywords;

  // Reusable symbols for code generation that can't be GC'ed during execution
  ObjectSymbol *quote_symbol;

  // The expander function to expand macros
  ObjectClosure *expander;

  // Module management
  Table modules;
  ObjectModule *root_module;
  ObjectModule *core_module;
  ObjectModule *current_module;
  ObjectCons *load_paths;

  // The most recent reset marker
  ObjectStackMarker *current_reset_marker;

  // An opaque pointer to the current compiler to avoid cyclic type
  // dependencies. Used to call the compiler's root marking function.
  void *current_compiler;

  // Tracks open upvalues to be closed on return
  ObjectUpvalue *open_upvalues;

  // Memory management
  Object *objects;
  int gray_count;
  int gray_capacity;
  Object **gray_stack;

  // The context for running the reader
  ReaderContext reader_context;

  // An application-specific context object
  void *app_context;

  // Process startup details
  int arg_count;
  char **arg_array;

  // Specifies whether the VM is currently running
  bool is_running;
} VM;

InterpretResult mesche_vm_call_closure(VM *vm, ObjectClosure *closure, int arg_count, Value *args);
InterpretResult mesche_vm_load_module(VM *vm, ObjectModule *module, const char *module_path);

#endif
