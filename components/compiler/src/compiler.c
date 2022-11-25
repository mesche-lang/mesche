#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "compiler.h"
#include "disasm.h"
#include "error.h"
#include "gc.h"
#include "keyword.h"
#include "mem.h"
#include "module.h"
#include "native.h"
#include "object.h"
#include "op.h"
#include "reader.h"
#include "string.h"
#include "syntax.h"
#include "util.h"
#include "vm-impl.h"

#define UINT8_COUNT (UINT8_MAX + 1)
#define MAX_AND_OR_EXPRS 100

#define ERROR_BUFFER_SIZE 128
char ERROR_BUFFER[ERROR_BUFFER_SIZE];

// NOTE: Enable this for diagnostic purposes
/* #define DEBUG_PRINT_CODE */

#define HEAD(_syntax)                                                                              \
  (IS_SYNTAX(_syntax) && IS_CONS(AS_SYNTAX(_syntax)->value)                                        \
       ? AS_CONS(AS_SYNTAX(_syntax)->value)->car                                                   \
       : UNSPECIFIED_VAL)

#define MAYBE_EMPTY(_syntax) (IS_SYNTAX(_syntax) && IS_EMPTY(AS_SYNTAX(_syntax)->value))

#define MAYBE_SYMBOL(_syntax, result_var)                                                          \
  {                                                                                                \
    Value syn = _syntax;                                                                           \
    result_var = NULL;                                                                             \
    if (IS_SYNTAX(syn) && IS_SYMBOL(AS_SYNTAX(syn)->value)) {                                      \
      result_var = AS_SYMBOL(AS_SYNTAX(syn)->value);                                               \
    }                                                                                              \
  }

#define EXPECT_SYMBOL(_ctx, _syntax, result_var, message)                                          \
  {                                                                                                \
    Value syn = _syntax;                                                                           \
    MAYBE_SYMBOL(_syntax, result_var);                                                             \
    if (result_var == NULL) {                                                                      \
      return compiler_error(_ctx, syn, message);                                                   \
    }                                                                                              \
  }

#define MAYBE_KEYWORD(_syntax, result_var)                                                         \
  {                                                                                                \
    Value syn = _syntax;                                                                           \
    result_var = NULL;                                                                             \
    if (IS_SYNTAX(syn) && IS_KEYWORD(AS_SYNTAX(syn)->value)) {                                     \
      result_var = AS_KEYWORD(AS_SYNTAX(syn)->value);                                              \
    }                                                                                              \
  }

#define MAYBE_STRING(_syntax, result_var)                                                          \
  {                                                                                                \
    Value syn = _syntax;                                                                           \
    result_var = NULL;                                                                             \
    if (IS_SYNTAX(syn) && IS_STRING(AS_SYNTAX(syn)->value)) {                                      \
      result_var = AS_STRING(AS_SYNTAX(syn)->value);                                               \
    }                                                                                              \
  }

#define EXPECT_EXPR(_ctx, _syntax, message)                                                        \
  {                                                                                                \
    Value syn = _syntax;                                                                           \
    if (!IS_SYNTAX(syn)) {                                                                         \
      return compiler_error(_ctx, syn, message);                                                   \
    }                                                                                              \
  }

#define EXPECT_CONS(_ctx, _syntax, message)                                                        \
  {                                                                                                \
    Value syn = _syntax;                                                                           \
    if (!IS_SYNTAX(syn) || !IS_CONS(AS_SYNTAX(syn)->value)) {                                      \
      return compiler_error(_ctx, syn, message);                                                   \
    }                                                                                              \
  }

#define EXPECT_EMPTY(_ctx, _syntax, message)                                                       \
  if (!IS_EMPTY(_syntax)) {                                                                        \
    return compiler_error(_ctx, FALSE_VAL, message);                                               \
  }

#define OK(_statement)                                                                             \
  {                                                                                                \
    Value result = _statement;                                                                     \
    if (IS_ERROR(result)) {                                                                        \
      return result;                                                                               \
    }                                                                                              \
  }

#define MOVE_NEXT(_ctx, _syntax)                                                                   \
  /* Move the list head to the next item but don't do any type checking */                         \
  if (IS_SYNTAX(_syntax) && IS_CONS(AS_SYNTAX(_syntax)->value)) {                                  \
    _syntax = AS_CONS(AS_SYNTAX(_syntax)->value)->cdr;                                             \
    /* PRINT_VALUE(_ctx->vm, "MOVE TO: ", _syntax); */                                             \
  }

// Stores details relating to a local variable binding in a scope
typedef struct {
  Value name;
  int depth;
  bool is_captured;
} Local;

// Stores details relating to a captured variable in a parent scope
typedef struct {
  uint8_t index;
  bool is_local;
} Upvalue;

// Stores context for compilation at a particular scope
typedef struct CompilerContext {
  struct CompilerContext *parent;

  VM *vm;
  MescheMemory *mem;

  // `module` should only be set when parsing a module body!  Sub-functions
  // should not have the same module set.
  ObjectModule *module; // NOTE: Might be null if not compiling module
  ObjectFunction *function;
  FunctionType function_type;
  ObjectString *file_name;

  Local locals[UINT8_COUNT];
  int local_count;
  int scope_depth;
  Upvalue upvalues[UINT8_COUNT];

  int tail_site_count;
  uint8_t tail_sites[UINT8_COUNT];
} CompilerContext;

typedef struct {
  void *result;
  MescheError *error;
} CompilerResult;

typedef struct {
  bool is_export;
  ObjectString *doc_string;
} DefineAttributes;

typedef enum { ARG_POSITIONAL, ARG_REST, ARG_KEYWORD } ArgType;

void mesche_compiler_mark_roots(void *target) {
  CompilerContext *ctx = (CompilerContext *)target;

  while (ctx != NULL) {
    mesche_gc_mark_object(ctx->vm, (Object *)ctx->function);

    if (ctx->module != NULL) {
      mesche_gc_mark_object(ctx->vm, (Object *)ctx->module);
    }

    ctx = ctx->parent;
  }
}

static void compiler_init_context(CompilerContext *ctx, CompilerContext *parent, FunctionType type,
                                  ObjectModule *module) {
  if (parent != NULL) {
    ctx->parent = parent;
    ctx->vm = parent->vm;
    ctx->file_name = parent->file_name;
  }

  // Set up the compiler state for this scope
  ctx->function = mesche_object_make_function(ctx->vm, type);
  ctx->function->chunk.file_name = ctx->file_name;
  ctx->function_type = type;
  ctx->module = module;
  ctx->local_count = 0;
  ctx->scope_depth = 0;
  ctx->tail_site_count = 0;

  // Set up memory management
  ctx->vm->current_compiler = ctx;
  ctx->mem = &ctx->vm->mem;

  // Establish the first local slot
  Local *local = &ctx->locals[ctx->local_count++];
  local->depth = 0;
  local->is_captured = false;
  local->name = FALSE_VAL;
}

static void compiler_emit_byte(CompilerContext *ctx, Value syntax, uint8_t byte) {
  int line = 0;
  ObjectString *file_name = NULL;
  if (IS_SYNTAX(syntax)) {
    line = AS_SYNTAX(syntax)->line;
    file_name = AS_SYNTAX(syntax)->file_name;
  }

  /* ctx->function->chunk.file_name = file_name; */
  mesche_chunk_write(ctx->mem, &ctx->function->chunk, byte, line);
}

static void compiler_emit_bytes(CompilerContext *ctx, Value syntax, uint8_t byte1, uint8_t byte2) {
  compiler_emit_byte(ctx, syntax, byte1);
  compiler_emit_byte(ctx, syntax, byte2);
}

static void compiler_emit_call(CompilerContext *ctx, Value syntax, uint8_t arg_count,
                               uint8_t keyword_count) {
  compiler_emit_byte(ctx, syntax, OP_CALL);
  compiler_emit_byte(ctx, syntax, arg_count);
  compiler_emit_byte(ctx, syntax, keyword_count);
}

static void compiler_log_tail_site(CompilerContext *ctx) {
  // Log the call site to potentially patch tail calls later.  The call has
  // already been written so look back 3 bytes (the size of an OP_CALL) to
  // check whether there actually was a call.
  int func_offset = ctx->function->chunk.count - 3;
  if (ctx->function->chunk.count >= 3 && ctx->function->chunk.code[func_offset] == OP_CALL) {
    // It's possible that a block parser could try to add a tail site that was
    // just added by another sub-expression, skip such an occurrence.
    if (ctx->tail_site_count == 0 || ctx->tail_sites[ctx->tail_site_count - 1] != func_offset) {
      ctx->tail_sites[ctx->tail_site_count] = func_offset;
      ctx->tail_site_count++;
    }
  }
}

static void compiler_patch_tail_calls(CompilerContext *ctx) {
  // Here's how it works:
  // - Parser code that knows it contains tail contexts will call parsing functions
  //   to parse sub-units of code (like define, lambda, let, if, etc).
  // - At the site of a tail context in one of these functions, compiler_log_tail_site
  //   will be called to log the offset where the last instruction written is a call,
  //   even if another function took care of parsing the expression at the tail context.
  // - Expressions that parse multiple sub-expressions in sequence (let, begin, lambda,
  //   and, or) will reset the tail_site count to its original location each time a new
  //   sub expression is encountered.  This ensures that only the tail sites from the
  //   last sub-expression will be patched.
  // - Once the parsing of an actual function is completed in compiler_end, patch any
  //   tail site offsets that remain in the tail_sites array!

  for (int i = 0; i < ctx->tail_site_count; i++) {
    // Convert the call site to a tail call
    ctx->function->chunk.code[ctx->tail_sites[i]] = OP_TAIL_CALL;
  }
}

static void compiler_emit_return(CompilerContext *ctx) {
  compiler_emit_byte(ctx, UNSPECIFIED_VAL, OP_RETURN);
}

static Value compiler_end(CompilerContext *ctx) {
  ObjectFunction *function = ctx->function;

  if (function->type == TYPE_FUNCTION) {
    // Patch tail calls in the function
    compiler_patch_tail_calls(ctx);
  }

  compiler_emit_return(ctx);

#ifdef DEBUG_PRINT_CODE
  mesche_disasm_function(ctx->vm->output_port, function);
#endif

  return OBJECT_VAL(function);
}

static uint8_t compiler_make_constant(CompilerContext *ctx, Value value) {
  int constant = mesche_chunk_constant_add(ctx->mem, &ctx->function->chunk, value);

  // TODO: We'll want to make sure we have at least 16 bits for constants
  if (constant > UINT8_MAX) {
    PANIC("Too many constants in one chunk!\n");
  }

  return (uint8_t)constant;
}

static void compiler_emit_constant(CompilerContext *ctx, Value syntax, Value value) {
  compiler_emit_bytes(ctx, syntax, OP_CONSTANT, compiler_make_constant(ctx, value));
}

static const char *prefix_message(const char *prefix, const char *message) {
  snprintf(ERROR_BUFFER, ERROR_BUFFER_SIZE, "%s: %s", prefix, message);
  return ERROR_BUFFER;
}

static Value compiler_error(CompilerContext *ctx, Value syntax, const char *message) {
  ObjectSyntax *real_syntax = IS_SYNTAX(syntax) ? AS_SYNTAX(syntax) : NULL;
  if (real_syntax) {
    return mesche_error(ctx->vm, "%s at line %d in %s.", message, real_syntax->line,
                        real_syntax->file_name ? real_syntax->file_name->chars : "(unknown)");
  } else {
    return mesche_error(ctx->vm, "%s.", message);
  }
}

// Predefine the main parser function
static Value compiler_parse_expr(CompilerContext *ctx, Value syntax);

static void compiler_emit_literal(CompilerContext *ctx, Value syntax) {
  Value value = AS_SYNTAX(syntax)->value;
  if (IS_TRUE(value)) {
    compiler_emit_byte(ctx, syntax, OP_TRUE);
  } else if (IS_FALSE(value)) {
    compiler_emit_byte(ctx, syntax, OP_FALSE);
  } else if (IS_EMPTY(value)) {
    compiler_emit_byte(ctx, syntax, OP_EMPTY);
  }
}

static Value compiler_parse_block(CompilerContext *ctx, Value syntax, const char *form_name) {
  int previous_tail_count = ctx->tail_site_count;

  EXPECT_CONS(ctx, syntax, prefix_message(form_name, "Expected at least one body expression"));
  for (;;) {
    // Reset the tail sites to log again for the sub-expression
    ctx->tail_site_count = previous_tail_count;

    // Parse the sub-expression
    OK(compiler_parse_expr(ctx, HEAD(syntax)));

    // Are we finished parsing expressions?
    MOVE_NEXT(ctx, syntax);
    if (IS_EMPTY(syntax)) {
      // Log the possible tail call if there should be one
      compiler_log_tail_site(ctx);
      break;
    } else {
      // If we continue the loop, pop the last expression result
      compiler_emit_byte(ctx, syntax, OP_POP);
    }
  }
}

static Value compiler_add_local(CompilerContext *ctx, Value syntax, ObjectSymbol *symbol) {
  if (ctx->local_count == UINT8_COUNT) {
    return compiler_error(ctx, syntax, "Too many local variables defined in function.");
  }

  Local *local = &ctx->locals[ctx->local_count++];
  local->name = OBJECT_VAL(symbol->name);
  local->depth = -1; // The variable is uninitialized until assigned
  local->is_captured = false;

  return TRUE_VAL;
}

static int compiler_resolve_local(CompilerContext *ctx, Value syntax, ObjectSymbol *symbol) {
  // Find the identifier by name in scope chain
  for (int i = ctx->local_count - 1; i >= 0; i--) {
    Local *local = &ctx->locals[i];
    if (mesche_value_eqv_p(OBJECT_VAL(symbol->name), local->name)) {
      if (local->depth == -1) {
        // TODO: Return the error!
        compiler_error(ctx, syntax, "Referenced variable before it was bound");
      }
      return i;
    }
  }

  // Is the name the same as the function name?
  ObjectString *func_name = ctx->function->name;
  if (func_name && mesche_value_eqv_p(OBJECT_VAL(func_name), OBJECT_VAL(symbol->name))) {
    // Slot 0 points to the function itself
    return 0;
  }

  return -1;
}

static int compiler_add_upvalue(CompilerContext *ctx, Value syntax, uint8_t index, bool is_local) {
  // Can we reuse an existing upvalue?
  int upvalue_count = ctx->function->upvalue_count;
  for (int i = 0; i < upvalue_count; i++) {
    Upvalue *upvalue = &ctx->upvalues[i];
    if (upvalue->index == index && upvalue->is_local == is_local) {
      return i;
    }
  }

  if (upvalue_count == UINT8_COUNT) {
    // TODO: Return the error!
    compiler_error(ctx, syntax, "Reached the limit of closures captured in one function.");
  }

  // Initialize the next upvalue slot
  ctx->upvalues[upvalue_count].is_local = is_local;
  ctx->upvalues[upvalue_count].index = index;
  return ctx->function->upvalue_count++;
}

static int compiler_resolve_upvalue(CompilerContext *ctx, Value syntax, ObjectSymbol *symbol) {
  // If there's no parent context then there's nothing to close over
  if (ctx->parent == NULL)
    return -1;

  // First try to resolve the variable as a local in the parent
  int local = compiler_resolve_local(ctx->parent, syntax, symbol);
  if (local != -1) {
    ctx->parent->locals[local].is_captured = true;
    return compiler_add_upvalue(ctx, syntax, (uint8_t)local, true);
  }

  // If we didn't find a local, look for a binding from a parent scope
  int upvalue = compiler_resolve_upvalue(ctx->parent, syntax, symbol);
  if (upvalue != -1) {
    return compiler_add_upvalue(ctx, syntax, (uint8_t)upvalue, false);
  }

  // No local or upvalue to bind to, assume global
  return -1;
}

static void compiler_local_mark_initialized(CompilerContext *ctx) {
  // If we're in global scope, don't do anything
  if (ctx->scope_depth == 0)
    return;

  // Mark the latest local variable as initialized
  ctx->locals[ctx->local_count - 1].depth = ctx->scope_depth;
}

static Value compiler_declare_variable(CompilerContext *ctx, Value syntax, ObjectSymbol *symbol) {
  // No need to declare a global, it will be dynamically resolved
  if (ctx->scope_depth == 0)
    return TRUE_VAL;

  // Start from the most recent local and work backwards to
  // find an existing variable with the same binding in this scope
  for (int i = ctx->local_count - 1; i >= 0; i--) {
    // If the local is in a different scope, stop looking
    Local *local = &ctx->locals[i];
    if (local->depth != -1 && local->depth < ctx->scope_depth)
      break;

    // In the same scope, is the identifier the same?
    if (mesche_value_eqv_p(OBJECT_VAL(symbol->name), local->name)) {
      return compiler_error(ctx, syntax, "Duplicate variable binding in 'let'");
    }
  }

  // Add a local binding for the name
  return compiler_add_local(ctx, syntax, symbol);
}

static uint8_t compiler_resolve_symbol(CompilerContext *ctx, Value syntax, ObjectSymbol *symbol,
                                       bool is_global) {
  // Declare the variable and exit if we're in a local scope
  if (!is_global && ctx->scope_depth > 0) {
    compiler_declare_variable(ctx, syntax, symbol);
    return 0;
  }

  // Reuse an existing constant for the same string if possible
  uint8_t constant = 0;
  bool value_found = false;
  Chunk *chunk = &ctx->function->chunk;
  Value symbol_name = OBJECT_VAL(symbol->name);
  for (int i = 0; i < chunk->constants.count; i++) {
    if (mesche_value_eqv_p(chunk->constants.values[i], symbol_name)) {
      constant = i;
      value_found = true;
      break;
    }
  }

  return value_found ? constant : compiler_make_constant(ctx, symbol_name);
}

static Value compiler_emit_identifier(CompilerContext *ctx, Value syntax) {
  ObjectSymbol *symbol;
  EXPECT_SYMBOL(ctx, syntax, symbol, "Expected identifier symbol");

  // Are we looking at a local variable?
  // TODO: Leverage lexical information in the syntax object
  int local_index = compiler_resolve_local(ctx, syntax, symbol);
  if (local_index != -1) {
    compiler_emit_bytes(ctx, syntax, OP_READ_LOCAL, (uint8_t)local_index);
  } else if ((local_index = compiler_resolve_upvalue(ctx, syntax, symbol)) != -1) {
    // Found an upvalue
    compiler_emit_bytes(ctx, syntax, OP_READ_UPVALUE, (uint8_t)local_index);
  } else {
    uint8_t variable_constant = compiler_resolve_symbol(ctx, syntax, symbol, true);
    compiler_emit_bytes(ctx, syntax, OP_READ_GLOBAL, variable_constant);
  }
}

static Value compiler_define_variable_ex(CompilerContext *ctx, Value syntax,
                                         uint8_t variable_constant,
                                         DefineAttributes *define_attributes) {
  // We don't define global variables in local scopes
  if (ctx->scope_depth > 0) {
    compiler_local_mark_initialized(ctx);
    return NUMBER_VAL(ctx->local_count - 1);
  }

  compiler_emit_bytes(ctx, syntax, OP_DEFINE_GLOBAL, variable_constant);

  if (define_attributes) {
    if (define_attributes->is_export) {
      // Add the binding to the module
      if (ctx->module != NULL) {
        mesche_value_array_write((MescheMemory *)ctx->vm, &ctx->module->exports,
                                 ctx->function->chunk.constants.values[variable_constant]);
      }

      // TODO: Convert to OP_CREATE_BINDING?
      /* compiler_emit_bytes(ctx, syntax, OP_EXPORT_SYMBOL, variable_constant); */
    }
  }

  // TODO: What does this really mean?
  return NUMBER_VAL(0);
}

static Value compiler_define_variable(CompilerContext *ctx, Value syntax,
                                      uint8_t variable_constant) {
  return compiler_define_variable_ex(ctx, syntax, variable_constant, NULL);
}

static Value compiler_parse_set(CompilerContext *ctx, Value syntax) {
  ObjectSymbol *symbol;
  EXPECT_SYMBOL(ctx, HEAD(syntax), symbol, "set!: Expected binding name");

  uint8_t instr = OP_SET_LOCAL;
  int arg = compiler_resolve_local(ctx, syntax, symbol);

  // If there isn't a local, use a global variable instead
  if (arg != -1) {
    // Do nothing, all values are already set.
  } else if ((arg = compiler_resolve_upvalue(ctx, syntax, symbol)) != -1) {
    instr = OP_SET_UPVALUE;
  } else {
    arg = compiler_resolve_symbol(ctx, syntax, symbol, true);
    instr = OP_SET_GLOBAL;
  }

  // Parse the value to set
  MOVE_NEXT(ctx, syntax);
  EXPECT_EXPR(ctx, HEAD(syntax), "set!: Expected expression for new value");
  OK(compiler_parse_expr(ctx, HEAD(syntax)));

  // Emit the setter
  compiler_emit_bytes(ctx, syntax, instr, arg);

  // Nothing else is expected after the value
  MOVE_NEXT(ctx, syntax);
  EXPECT_EMPTY(ctx, syntax, "set!: Expected end of expression");

  return TRUE_VAL;
}

// TODO: This was used for the previous implementation, might be deletable!

static void compiler_begin_scope(CompilerContext *ctx) { ctx->scope_depth++; }

static void compiler_end_scope(CompilerContext *ctx) {
  // Pop all local variables from the previous scope while closing any upvalues
  // that have been captured inside of it
  ctx->scope_depth--;
  uint8_t local_count = 0;
  while (ctx->local_count > 0 && ctx->locals[ctx->local_count - 1].depth > ctx->scope_depth) {
    local_count++;
    if (ctx->locals[ctx->local_count - 1].is_captured) {
      compiler_emit_byte(ctx, UNSPECIFIED_VAL, OP_CLOSE_UPVALUE);
    } else {
      compiler_emit_bytes(ctx, UNSPECIFIED_VAL, OP_POP_SCOPE, 1);
    }
    ctx->local_count--;
  }
}

static Value compiler_parse_let(CompilerContext *ctx, Value syntax) {
  int previous_tail_count = ctx->tail_site_count;
  Value let_expr = syntax;

  // Create a new compiler context for parsing the let body as an inline
  // function
  CompilerContext let_ctx;
  compiler_init_context(&let_ctx, ctx, TYPE_FUNCTION, NULL);
  compiler_begin_scope(&let_ctx);

  // Parse the name of the let, if any
  ObjectSymbol *let_name;
  MAYBE_SYMBOL(HEAD(syntax), let_name);
  if (let_name) {
    // Use the symbol name as the let function's name
    let_ctx.function->name = let_name->name;
    MOVE_NEXT(ctx, syntax);
  } else {
    EXPECT_CONS(ctx, syntax, "let: Expected symbol or binding list");
  }

  // Before parsing arguments, skip ahead to leave enough space to write out the
  // OP_CLOSURE instruction with the right constant value before writing out the
  // OP_CALL instruction.
  int func_offset = ctx->function->chunk.count;
  ctx->function->chunk.count += 2;

  // Allow empty binding list
  if (!MAYBE_EMPTY(HEAD(syntax))) {
    EXPECT_CONS(ctx, HEAD(syntax), "let: Expected binding list");

    // Process the binding list
    Value bindings = HEAD(syntax);
    if (!IS_SYNTAX(bindings) || !IS_EMPTY(AS_SYNTAX(bindings)->value)) {
      while (!IS_EMPTY(bindings)) {
        ObjectCons *binding_list;
        EXPECT_CONS(ctx, bindings, "let: Expected binding pair");

        // Increase the binding (function argument) count
        let_ctx.function->arity++;
        if (let_ctx.function->arity > 255) {
          return compiler_error(&let_ctx, bindings, "Exceeded maximum of 255 let bindings");
        }

        // Parse the symbol for the binding
        // TODO: Support binding without value?
        Value binding = HEAD(bindings);
        EXPECT_CONS(ctx, binding, "let: Expected binding pair");

        ObjectSymbol *symbol;
        EXPECT_SYMBOL(ctx, HEAD(binding), symbol, "let: Expected symbol for binding pair");

        uint8_t constant = compiler_resolve_symbol(&let_ctx, HEAD(binding), symbol, false);
        compiler_define_variable(&let_ctx, HEAD(binding), constant);

        // This is a tricky situation: we are going to parse the next expression
        // in the parent context and it's possible that another function will be
        // parsed as a let binding value.  This will cause the current let_ctx to
        // be removed from the ctx chain so it won't be marked in GC sweeps.  To
        // avoid the let_ctx function being reclaimed during this time, we need to
        // push it to the value stack.
        mesche_vm_stack_push(let_ctx.vm, OBJECT_VAL(let_ctx.function));

        // Parse the binding value into the *original context* where the let
        // function will be called so that it gets passed as a parameter to the
        // lambda.
        MOVE_NEXT(ctx, binding);
        EXPECT_CONS(ctx, binding, "let: Expected expression for binding value");
        OK(compiler_parse_expr(ctx, HEAD(binding)));

        // Pop the function back off the stack and restore the correct context
        mesche_vm_stack_pop(let_ctx.vm);
        let_ctx.vm->current_compiler = &let_ctx;

        // Reset the tail site count so that the binding expression will not be
        // treated as a tail call site
        ctx->tail_site_count = previous_tail_count;

        // Move to the next binding in the list
        MOVE_NEXT(ctx, bindings)
      }
    }

    // Move forward to reach the body expressions
    MOVE_NEXT(ctx, syntax);
  }

  // Parse the let body
  OK(compiler_parse_block(&let_ctx, syntax, "let"));

  // Finish the function and write out its constant and closure back where it
  // needs to go
  Value new_func = compiler_end(&let_ctx);
  OK(new_func);
  ObjectFunction *function = AS_FUNCTION(new_func);

  // If the let body function has upvalues, we need to extend the size of the
  // chunk to accomodate the upvalue opcodes
  if (function->upvalue_count > 0) {
    // Ensure the chunk is big enough to insert the upvalue opcodes
    int upvalue_size = function->upvalue_count * 2;
    mesche_chunk_insert_space(ctx->mem, &ctx->function->chunk, func_offset + 2, upvalue_size);
  }

  int call_offset = ctx->function->chunk.count;
  ctx->function->chunk.count = func_offset;
  compiler_emit_bytes(ctx, syntax, OP_CLOSURE, compiler_make_constant(ctx, OBJECT_VAL(function)));

  // Write out the references to each upvalue as arguments to OP_CLOSURE
  for (int i = 0; i < function->upvalue_count; i++) {
    compiler_emit_byte(ctx, syntax, let_ctx.upvalues[i].is_local ? 1 : 0);
    compiler_emit_byte(ctx, syntax, let_ctx.upvalues[i].index);
  }

  // Let the VM know we're back to the parent compiler
  ctx->vm->current_compiler = ctx;

  // Restore the offset where OP_CALL should be and write it
  ctx->function->chunk.count = call_offset;
  compiler_emit_call(ctx, let_expr, let_ctx.function->arity, 0);
  compiler_log_tail_site(ctx);
}

static Value compiler_parse_define_attributes(CompilerContext *ctx, Value syntax,
                                              DefineAttributes *define_attributes) {
  while (!IS_EMPTY(syntax)) {
    // Look for :export keyword
    ObjectKeyword *keyword;
    MAYBE_KEYWORD(HEAD(syntax), keyword);
    if (keyword) {
      if (memcmp(keyword->string.chars, "export", 7) == 0) {
        define_attributes->is_export = true;
      } else {
        // TODO: Warn on unknown keywords?
      }

      MOVE_NEXT(ctx, syntax);
    } else {
      break;
    }
  }

  // Look for a docstring
  if (!IS_EMPTY(syntax)) {
    ObjectString *string;
    MAYBE_STRING(HEAD(syntax), string);
    if (string) {
      // Store it as the documentation string
      define_attributes->doc_string = string;
      MOVE_NEXT(ctx, syntax);
    }
  }

  return syntax;
}

static bool compiler_compare_keyword(Token keyword, const char *expected_name) {
  return memcmp(keyword.start + 1, expected_name, strlen(expected_name)) == 0;
}

static Value compiler_parse_lambda_inner(CompilerContext *ctx, Value formals, Value syntax,
                                         ObjectString *name, const char *form_name) {
  // Create a new compiler context for parsing the function body
  CompilerContext func_ctx;
  compiler_init_context(&func_ctx, ctx, TYPE_FUNCTION, NULL);
  compiler_begin_scope(&func_ctx);

  // Parse the parameter list, one of the following:
  // - A list of symbols: (a b c)
  // - An improper list consed with a symbol: (a b . c)
  // - A single symbol as "rest" param: a
  ObjectSymbol *param;
  MAYBE_SYMBOL(formals, param);
  if (param != NULL) {
    // The formals list isn't a list, it's a single rest parameter
    uint8_t constant = compiler_resolve_symbol(&func_ctx, formals, param, false);
    compiler_define_variable(&func_ctx, formals, constant);
    func_ctx.function->rest_arg_index = 1;
    func_ctx.function->arity++;
  } else if (!MAYBE_EMPTY(formals)) {
    while (!IS_EMPTY(formals)) {
      Value this_param;
      bool is_rest_param = false;
      MAYBE_SYMBOL(formals, param);
      if (param) {
        // Now that we've found the rest parameter, exit after processing it
        this_param = formals;
        is_rest_param = true;
        formals = EMPTY_VAL;
      } else {
        const char *message = prefix_message(form_name, "Expected symbol in parameter list");
        EXPECT_CONS(ctx, formals, message);
        EXPECT_SYMBOL(ctx, HEAD(formals), param, message);
        this_param = HEAD(formals);
      }

      uint8_t constant = compiler_resolve_symbol(&func_ctx, this_param, param, false);
      compiler_define_variable(&func_ctx, this_param, constant);

      func_ctx.function->arity++;
      if (func_ctx.function->arity > 255) {
        return compiler_error(&func_ctx, syntax,
                              prefix_message(form_name, "Function has more than 255 parameters"));
      }

      // Store knowledge of the "rest" parameter
      if (is_rest_param) {
        func_ctx.function->rest_arg_index = func_ctx.function->arity;
      } else {
        // Move to the next parameter
        MOVE_NEXT(ctx, formals);
      }
    }
  }

  // Parse the body (just pass syntax directly)
  OK(compiler_parse_block(&func_ctx, syntax, form_name));

  // Get the parsed function and store it in a constant
  func_ctx.function->name = name;
  // TODO: Check for error!
  ObjectFunction *function = AS_FUNCTION(compiler_end(&func_ctx));
  compiler_emit_bytes(ctx, syntax, OP_CLOSURE, compiler_make_constant(ctx, OBJECT_VAL(function)));

  // Write out the references to each upvalue as arguments to OP_CLOSURE
  for (int i = 0; i < function->upvalue_count; i++) {
    compiler_emit_byte(ctx, syntax, func_ctx.upvalues[i].is_local ? 1 : 0);
    compiler_emit_byte(ctx, syntax, func_ctx.upvalues[i].index);
  }

  // Let the VM know we're back to the parent compiler
  ctx->vm->current_compiler = ctx;

  return TRUE_VAL;
}

static Value compiler_parse_lambda(CompilerContext *ctx, Value syntax) {
  // Enter the formals list before parsing the lambda.  The inner lambda
  // function will pop the syntax once it parses the formals list.
  EXPECT_CONS(ctx, syntax, "lambda: Expected symbol or parameter list");

  Value formals = HEAD(syntax);
  MOVE_NEXT(ctx, syntax);
  return compiler_parse_lambda_inner(ctx, formals, syntax, NULL, "lambda");
}

static Value compiler_parse_quote(CompilerContext *ctx, Value syntax) {
  EXPECT_CONS(ctx, syntax, "quote: Expected expression");
  Value quoted = mesche_syntax_to_datum(ctx->vm, HEAD(syntax));
  compiler_emit_constant(ctx, syntax, quoted);

  MOVE_NEXT(ctx, syntax);
  EXPECT_EMPTY(ctx, syntax, "quote: Expected end of expression");

  return TRUE_VAL;
}

static Value compiler_parse_apply(CompilerContext *ctx, Value syntax) {
  // Parse the function expression
  ObjectSymbol *symbol;
  EXPECT_CONS(ctx, syntax, "apply: Expected callee expression");
  OK(compiler_parse_expr(ctx, HEAD(syntax)));

  // Parse the argument list
  MOVE_NEXT(ctx, syntax);
  EXPECT_CONS(ctx, syntax, "apply: Expected argument expression");
  OK(compiler_parse_expr(ctx, HEAD(syntax)));

  compiler_emit_byte(ctx, syntax, OP_APPLY);

  MOVE_NEXT(ctx, syntax);
  EXPECT_EMPTY(ctx, syntax, "apply: Expected end of expression");

  return TRUE_VAL;
}

static Value compiler_parse_reset(CompilerContext *ctx, Value syntax) {
  EXPECT_CONS(ctx, syntax, "reset: Expected lambda expression");

  // `reset` requires a lambda expression with no arguments
  ObjectSymbol *symbol;
  Value lambda = HEAD(syntax);
  MAYBE_SYMBOL(HEAD(lambda), symbol);
  if (!symbol || symbol->token_kind != TokenKindLambda) {
    return compiler_error(ctx, syntax, "reset: Expected lambda expression");
  }

  // Parse the lambda body of the reset expression *after* emitting OP_RESET so
  // that we can set the new reset context before adding the closure to the
  // stack.
  compiler_emit_byte(ctx, syntax, OP_RESET);
  MOVE_NEXT(ctx, lambda);
  OK(compiler_parse_lambda(ctx, lambda));
  compiler_emit_call(ctx, syntax, 0, 0);
  compiler_emit_byte(ctx, syntax, OP_NOP); // Avoid turning this into a tail call!

  // End of expression
  /* MOVE_NEXT(ctx); */
  /* EXPECT_EMPTY(ctx); */

  return TRUE_VAL;
}

static Value compiler_parse_shift(CompilerContext *ctx, Value syntax) {
  EXPECT_CONS(ctx, syntax, "shift: Expected lambda expression");

  // `shift` requires a lambda expression with one argument
  ObjectSymbol *symbol;
  Value lambda = HEAD(syntax);
  MAYBE_SYMBOL(HEAD(lambda), symbol);
  if (!symbol || symbol->token_kind != TokenKindLambda) {
    return compiler_error(ctx, syntax, "Expected lambda expression after 'shift'");
  }

  // Parse the lambda body of the shift expression *before* emitting OP_SHIFT so
  // that the shift body will be invoked with the continuation function as its
  // parameter.
  MOVE_NEXT(ctx, lambda);
  OK(compiler_parse_lambda(ctx, lambda));
  compiler_emit_byte(ctx, syntax, OP_SHIFT);
  compiler_emit_call(ctx, syntax, 1, 0);
  compiler_emit_byte(ctx, syntax, OP_NOP); // Avoid turning this into a tail call!

  // End of expression
  /* MOVE_NEXT(ctx); */
  /* EXPECT_EMPTY(ctx); */

  return TRUE_VAL;
}

static Value compiler_parse_define(CompilerContext *ctx, Value syntax) {
  DefineAttributes define_attributes;
  define_attributes.is_export = false;
  define_attributes.doc_string = NULL;

  // Make sure something comes after `define`
  EXPECT_CONS(ctx, syntax, "define: Expected symbol or function parameter list");

  // TODO: Only allow defines at the top of let/lambda bodies
  // TODO: Check ctx for this!

  // There are two paths here:
  // - Define a variable binding, parse value expression, parse attributes
  // - Define a function binding, parse first symbol of formals list, parse attributes, parse rest
  //   as lambda
  ObjectSymbol *symbol;
  MAYBE_SYMBOL(HEAD(syntax), symbol);
  if (symbol == NULL) {
    Value formals = HEAD(syntax);
    EXPECT_CONS(ctx, formals, "define: Expected symbol or function parameter list");

    // Take the binding's symbol and create a constant for it
    Value func_name = HEAD(formals);
    EXPECT_SYMBOL(ctx, func_name, symbol, "define: Expected function name symbol");
    uint8_t variable_constant = compiler_resolve_symbol(ctx, syntax, symbol, true);

    // Parse any binding attributes
    MOVE_NEXT(ctx, syntax);
    syntax = compiler_parse_define_attributes(ctx, syntax, &define_attributes);
    OK(syntax);

    // Parse the remaining function definition
    MOVE_NEXT(ctx, formals);
    OK(compiler_parse_lambda_inner(ctx, formals, syntax, symbol->name, "define"));

    // Finish the binding definition
    OK(compiler_define_variable_ex(ctx, func_name, variable_constant, &define_attributes));
  } else {
    // Create a constant for the binding symbol
    uint8_t variable_constant = compiler_resolve_symbol(ctx, syntax, symbol, true);

    // Parse the value expression for the binding
    MOVE_NEXT(ctx, syntax);
    Value expr = syntax;
    EXPECT_CONS(ctx, expr, "define: Expected expression for binding");
    OK(compiler_parse_expr(ctx, HEAD(expr)));

    // Parse any binding attributes
    MOVE_NEXT(ctx, syntax);
    OK(compiler_parse_define_attributes(ctx, syntax, &define_attributes));

    // Finish the binding definition
    OK(compiler_define_variable_ex(ctx, expr, variable_constant, &define_attributes));
  }

  return TRUE_VAL;
}

static Value compiler_parse_module_name(CompilerContext *ctx, Value syntax, const char *form_name) {
  ObjectSymbol *symbol = NULL;
  uint8_t symbol_count = 0;
  char *module_name = NULL;
  Value original = syntax;

  const char *error_msg = prefix_message(form_name, "Expected module specifier (list of symbols)");
  EXPECT_CONS(ctx, syntax, error_msg);

  while (!IS_EMPTY(syntax)) {
    MAYBE_SYMBOL(HEAD(syntax), symbol);
    if (symbol == NULL) {
      // Free the current string allocation before exiting
      if (module_name != NULL)
        free(module_name);

      return compiler_error(ctx, HEAD(syntax), error_msg);
    }

    if (symbol_count == 0) {
      module_name = malloc(sizeof(char) * (symbol->name->length + 1));
      memcpy(module_name, symbol->name->chars, sizeof(char) * symbol->name->length);
      module_name[symbol->name->length] = '\0';
    } else {
      char *prev_name = module_name;
      module_name = mesche_cstring_join(module_name, strlen(module_name), symbol->name->chars,
                                        symbol->name->length, " ");
      free(prev_name);
    }

    // Move to the next symbol in the list
    MOVE_NEXT(ctx, syntax);
    symbol_count++;
  }

  if (module_name != NULL) {
    // If a module name was parsed, emit the constant module name string
    ObjectString *module_name_str =
        mesche_object_make_string(ctx->vm, module_name, strlen(module_name));
    compiler_emit_constant(ctx, original, OBJECT_VAL(module_name_str));

    // The module name has been copied, so free the temporary string
    free(module_name);
  } else {
    return compiler_error(ctx, original, "No symbols were found where module name was expected");
  }

  return TRUE_VAL;
}

static Value compiler_parse_module_import(CompilerContext *ctx, Value syntax) {
  EXPECT_CONS(ctx, syntax, "module-import: Expected module specifier (list of symbols)");
  OK(compiler_parse_module_name(ctx, HEAD(syntax), "module-import"));
  compiler_emit_byte(ctx, syntax, OP_IMPORT_MODULE);

  return TRUE_VAL;
}

static Value compiler_parse_module_enter(CompilerContext *ctx, Value syntax) {
  EXPECT_CONS(ctx, syntax, "module-enter: Expected module specifier (list of symbols)");
  OK(compiler_parse_module_name(ctx, HEAD(syntax), "module-enter"));
  compiler_emit_byte(ctx, syntax, OP_ENTER_MODULE);

  return TRUE_VAL;
}

static Value compiler_parse_define_module(CompilerContext *ctx, Value syntax) {
  // Read the symbol list for the module path
  OK(compiler_parse_module_name(ctx, HEAD(syntax), "define-module"));
  compiler_emit_byte(ctx, syntax, OP_DEFINE_MODULE);

  // Store the module name if we're currently parsing a module file
  if (ctx->module) {
    ctx->module->name =
        AS_STRING(ctx->function->chunk.constants.values[ctx->function->chunk.constants.count - 1]);
  }

  // Check for a possible 'import' expression
  MOVE_NEXT(ctx, syntax);
  if (!IS_EMPTY(syntax)) {
    EXPECT_CONS(ctx, syntax, "define-module:  Expected 'import' expression");

    // Verify that the first expression is the symbol `import`
    ObjectSymbol *symbol;
    Value imports = HEAD(syntax);
    EXPECT_SYMBOL(ctx, HEAD(imports), symbol, "define-module: Expected 'import' expression");
    if (symbol->token_kind != TokenKindImport) {
      return compiler_error(ctx, HEAD(imports), "define-module: Expected 'import' expression");
    }

    // There can be multiple import specifications
    MOVE_NEXT(ctx, imports);
    EXPECT_CONS(ctx, imports,
                "define-module: Expected module specifier (list of symbols) after 'import'.");
    while (!IS_EMPTY(imports)) {
      OK(compiler_parse_module_name(ctx, HEAD(imports), "define-module"));
      compiler_emit_byte(ctx, syntax, OP_IMPORT_MODULE);
      compiler_emit_byte(ctx, imports, OP_POP);

      MOVE_NEXT(ctx, imports);
    }
  }

  return TRUE_VAL;
}

// TODO: This should be replaced with a syntax-case
static Value compiler_parse_define_record_type(CompilerContext *ctx, Value syntax) {
  Value definition = syntax;

  // Parse and emit the name symbol
  ObjectSymbol *record_name;
  EXPECT_SYMBOL(ctx, HEAD(syntax), record_name, "define-record-type: Expected record name");
  compiler_emit_constant(ctx, syntax, OBJECT_VAL(record_name));

  // Move into the field list
  MOVE_NEXT(ctx, syntax);
  Value fields = HEAD(syntax);

  // Grab the 'fields' symbol
  Value sym = HEAD(fields);
  ObjectSymbol *fields_symbol;
  EXPECT_SYMBOL(ctx, sym, fields_symbol, "define-record-type: Expected 'fields' list");

  uint8_t field_count = 0;
  if (fields_symbol && strncmp(fields_symbol->name->chars, "fields", 6) == 0) {
    // Read the key-value pairs inside of the form
    MOVE_NEXT(ctx, fields);
    while (!IS_EMPTY(fields)) {
      ObjectSymbol *field_name;
      EXPECT_SYMBOL(ctx, HEAD(fields), field_name,
                    "define-record-type: Expected symbol for field name");
      if (field_name) {
        compiler_emit_constant(ctx, syntax, OBJECT_VAL(field_name));
        compiler_emit_constant(ctx, syntax, FALSE_VAL);
        field_count++;
      }

      MOVE_NEXT(ctx, fields);
    }

    if (field_count == 0) {
      return compiler_error(ctx, definition, "define-record-type: Expected at least one field");
    }
  } else {
    return compiler_error(ctx, sym, "define-record-type: Expected 'fields' list");
  }

  compiler_emit_bytes(ctx, definition, OP_DEFINE_RECORD, field_count);

  // Exit the inner list and ensure we've reach the end of the definition
  MOVE_NEXT(ctx, syntax);
  EXPECT_EMPTY(ctx, syntax, "define-record-type: Expected end of expression");

  return TRUE_VAL;
}

static int compiler_emit_jump(CompilerContext *ctx, Value syntax, uint8_t instruction) {
  // Write out the two bytes that will be patched once the jump target location
  // is determined
  compiler_emit_byte(ctx, syntax, instruction);
  compiler_emit_byte(ctx, syntax, 0xff);
  compiler_emit_byte(ctx, syntax, 0xff);

  return ctx->function->chunk.count - 2;
}

static Value compiler_patch_jump(CompilerContext *ctx, Value syntax, int offset) {
  // We offset by -2 to adjust for the size of the jump offset itself
  int jump = ctx->function->chunk.count - offset - 2;

  if (jump > UINT16_MAX) {
    return compiler_error(ctx, syntax, "Can't emit jump that is larger than possible jump size");
  }

  // Place the two bytes with the jump delta
  ctx->function->chunk.code[offset] = (jump >> 8) & 0xff;
  ctx->function->chunk.code[offset + 1] = jump & 0xff;

  return TRUE_VAL;
}

static Value compiler_parse_if(CompilerContext *ctx, Value syntax) {
  int previous_tail_count = ctx->tail_site_count;

  // Parse predicate and write out a jump instruction to the else code
  // path if the predicate evaluates to false
  OK(compiler_parse_expr(ctx, HEAD(syntax)));
  int jump_origin = compiler_emit_jump(ctx, HEAD(syntax), OP_JUMP_IF_FALSE);

  // Restore the tail count because the predicate expression should never
  // produce a tail call
  ctx->tail_site_count = previous_tail_count;

  // Include a pop so that the expression value gets removed from the stack in
  // the truth path
  compiler_emit_byte(ctx, HEAD(syntax), OP_POP);

  // Parse truth expr and log the tail call if the expression was still in a
  // tail context afterward
  MOVE_NEXT(ctx, syntax);
  EXPECT_CONS(ctx, syntax, "if: Expected consequent expression");
  OK(compiler_parse_expr(ctx, HEAD(syntax)));
  compiler_log_tail_site(ctx);

  // Write out a jump from the end of the truth case to the end of the expression
  int else_jump = compiler_emit_jump(ctx, syntax, OP_JUMP);

  // Patch the jump instruction which leads to the else path
  OK(compiler_patch_jump(ctx, syntax, jump_origin));

  // Include a pop so that the predicate expression value gets removed from the stack
  compiler_emit_byte(ctx, syntax, OP_POP);

  // Is there an else expression?
  MOVE_NEXT(ctx, syntax);
  if (!IS_EMPTY(syntax)) {
    OK(compiler_parse_expr(ctx, HEAD(syntax)));
    compiler_log_tail_site(ctx);
  } else {
    // Push a '#f' onto the value stack if there was no else
    compiler_emit_byte(ctx, syntax, OP_FALSE);
  }

  // Patch the jump instruction after the false path has been compiled
  OK(compiler_patch_jump(ctx, syntax, else_jump));

  return TRUE_VAL;
}

static Value compiler_parse_or(CompilerContext *ctx, Value syntax) {
  int prev_jump = -1;
  int expr_count = 0;
  int end_jumps[MAX_AND_OR_EXPRS];

  while (!IS_EMPTY(syntax)) {
    if (expr_count > 0) {
      // If it evaluates to false, jump to the next expression
      prev_jump = compiler_emit_jump(ctx, HEAD(syntax), OP_JUMP_IF_FALSE);

      // If it evaluates to true, jump to the end of the expression list
      end_jumps[expr_count - 1] = compiler_emit_jump(ctx, HEAD(syntax), OP_JUMP);

      // Patch the previous expression's jump
      OK(compiler_patch_jump(ctx, HEAD(syntax), prev_jump));

      // Emit a pop to remove the previous expression value from the stack
      compiler_emit_byte(ctx, HEAD(syntax), OP_POP);
    }

    // Parse the expression
    OK(compiler_parse_expr(ctx, HEAD(syntax)));
    expr_count++;

    if (expr_count == MAX_AND_OR_EXPRS) {
      return compiler_error(ctx, syntax, "Exceeded the maximum number of expressions in an `and`.");
    }

    // Consume the next operand in the list
    MOVE_NEXT(ctx, syntax);
  }

  // The last expression is a tail site
  compiler_log_tail_site(ctx);

  for (int i = 0; i < expr_count - 1; i++) {
    // Patch all the jumps to the end location
    OK(compiler_patch_jump(ctx, syntax, end_jumps[i]));
  }

  return TRUE_VAL;
}

static Value compiler_parse_and(CompilerContext *ctx, Value syntax) {
  int prev_jump = -1;
  int expr_count = 0;
  int end_jumps[MAX_AND_OR_EXPRS];

  while (!IS_EMPTY(syntax)) {
    if (expr_count > 0) {
      // If it evaluates to false, jump to the end of the expression list
      end_jumps[expr_count - 1] = compiler_emit_jump(ctx, syntax, OP_JUMP_IF_FALSE);

      // Emit a pop to remove the previous expression value from the stack
      compiler_emit_byte(ctx, syntax, OP_POP);
    }

    // Parse the expression
    OK(compiler_parse_expr(ctx, HEAD(syntax)));
    expr_count++;

    if (expr_count == MAX_AND_OR_EXPRS) {
      return compiler_error(ctx, syntax, "Exceeded the maximum number of expressions in an `and`");
    }

    MOVE_NEXT(ctx, syntax);
  }

  if (expr_count > 0) {
    // The last expression is a tail site
    compiler_log_tail_site(ctx);

    for (int i = 0; i < expr_count - 1; i++) {
      // Patch all the jumps to the end location
      OK(compiler_patch_jump(ctx, syntax, end_jumps[i]));
    }
  }

  return TRUE_VAL;
}

static bool compiler_emit_operator_call(CompilerContext *ctx, Value syntax, ObjectSymbol *symbol,
                                        uint8_t operand_count) {
  switch (symbol->token_kind) {
  case TokenKindPlus:
    compiler_emit_byte(ctx, syntax, OP_ADD);
    break;
  case TokenKindMinus:
    compiler_emit_byte(ctx, syntax, OP_SUBTRACT);
    break;
  case TokenKindStar:
    compiler_emit_byte(ctx, syntax, OP_MULTIPLY);
    break;
  case TokenKindSlash:
    compiler_emit_byte(ctx, syntax, OP_DIVIDE);
    break;
  case TokenKindPercent:
    compiler_emit_byte(ctx, syntax, OP_MODULO);
    break;
  case TokenKindNot:
    compiler_emit_byte(ctx, syntax, OP_NOT);
    break;
  case TokenKindGreaterThan:
    compiler_emit_byte(ctx, syntax, OP_GREATER_THAN);
    break;
  case TokenKindGreaterEqual:
    compiler_emit_byte(ctx, syntax, OP_GREATER_EQUAL);
    break;
  case TokenKindLessThan:
    compiler_emit_byte(ctx, syntax, OP_LESS_THAN);
    break;
  case TokenKindLessEqual:
    compiler_emit_byte(ctx, syntax, OP_LESS_EQUAL);
    break;
  case TokenKindList:
    compiler_emit_bytes(ctx, syntax, OP_LIST, operand_count);
    break;
  case TokenKindCons:
    compiler_emit_byte(ctx, syntax, OP_CONS);
    break;
  case TokenKindEqv:
    compiler_emit_byte(ctx, syntax, OP_EQV);
    break;
  case TokenKindEqual:
    compiler_emit_byte(ctx, syntax, OP_EQUAL);
    break;
  case TokenKindDisplay:
    compiler_emit_byte(ctx, syntax, OP_DISPLAY);
    break;
  default:
    return false;
  }

  return true;
}

// TODO: This should be in `core`?
static Value compiler_parse_load_file(CompilerContext *ctx, Value syntax) {
  EXPECT_CONS(ctx, syntax, "load-file: Expected expression");
  OK(compiler_parse_expr(ctx, HEAD(syntax)));
  compiler_emit_byte(ctx, syntax, OP_LOAD_FILE);

  MOVE_NEXT(ctx, syntax);
  EXPECT_EMPTY(ctx, syntax, "load-file: Expected end of expression");
}

static Value compiler_parse_list(CompilerContext *ctx, Value syntax) {
  // Possibilities
  // - Primitive command with its own opcode
  // - Special form that has non-standard call semantics
  // - Symbol in first position
  // - Raw lambda in first position
  // - Expression that evaluates to lambda
  // In the latter 3 cases, compiler the callee before the arguments

  Value call_expr = syntax;

  // Store the callee expression and move the syntax forward to the arguments
  Value callee = HEAD(syntax);
  MOVE_NEXT(ctx, syntax);

  // Peek at the first element to see if it's a symbol
  ObjectSymbol *symbol;
  MAYBE_SYMBOL(callee, symbol);
  if (symbol) {
    // Is the first item a symbol that refers to a core syntax or special form?
    switch (symbol->token_kind) {
    case TokenKindOr:
      return compiler_parse_or(ctx, syntax);
    case TokenKindAnd:
      return compiler_parse_and(ctx, syntax);
    case TokenKindLambda:
      return compiler_parse_lambda(ctx, syntax);
    case TokenKindLet:
      return compiler_parse_let(ctx, syntax);
    case TokenKindDefine:
      return compiler_parse_define(ctx, syntax);
    case TokenKindSet:
      return compiler_parse_set(ctx, syntax);
    case TokenKindIf:
      return compiler_parse_if(ctx, syntax);
    case TokenKindBegin:
      return compiler_parse_block(ctx, syntax, "begin");
    case TokenKindQuote:
      return compiler_parse_quote(ctx, syntax);
    case TokenKindApply:
      return compiler_parse_apply(ctx, syntax);
    case TokenKindShift:
      return compiler_parse_shift(ctx, syntax);
    case TokenKindReset:
      return compiler_parse_reset(ctx, syntax);
    case TokenKindDefineModule:
      return compiler_parse_define_module(ctx, syntax);
    case TokenKindModuleImport:
      return compiler_parse_module_import(ctx, syntax);
    case TokenKindModuleEnter:
      return compiler_parse_module_enter(ctx, syntax);
    case TokenKindDefineRecordType:
      return compiler_parse_define_record_type(ctx, syntax);
    case TokenKindLoadFile:
      return compiler_parse_load_file(ctx, syntax);
    case TokenKindBreak:
      // Just emit OP_BREAK and be done
      compiler_emit_byte(ctx, syntax, OP_BREAK);
      return TRUE_VAL;
    default:
      // Not all token types are handled
      break;
    }
  }

  // If we didn't see an operator, evaluate the callee expression to get the
  // procedure we need to invoke
  bool is_operator = symbol && symbol->token_kind != TokenKindNone;
  if (!is_operator) {
    OK(compiler_parse_expr(ctx, callee));
  }

  // Parse argument expressions until we reach the end of the list
  uint8_t arg_count = 0;
  while (!IS_EMPTY(syntax)) {
    // Compile next positional parameter
    OK(compiler_parse_expr(ctx, HEAD(syntax)));

    arg_count++;
    if (arg_count == 255) {
      return compiler_error(ctx, syntax, "Cannot pass more than 255 arguments in a function call");
    }

    MOVE_NEXT(ctx, syntax);
  }

  // Finally, emit the OP_CALL if we're invoking a procedure
  if (is_operator) {
    // Emit the operator call
    is_operator = compiler_emit_operator_call(ctx, call_expr, symbol, arg_count);
  }

  // One last chance to decide that this should be an OP_CALL
  if (!is_operator) {
    compiler_emit_call(ctx, syntax, arg_count, 0);
  }
}

static Value compiler_parse_expr(CompilerContext *ctx, Value syntax) {
  if (IS_EMPTY(syntax)) {
    PANIC("Parser function did not catch empty when parsing expression!")
  }

  // Try expanding the expression before parsing it.  This enables syntax
  // transformers which return a single value like `(and 3)`.
  if (ctx->vm->expander && IS_CONS(AS_SYNTAX(syntax)->value)) {
    /* printf("BEFORE EXPAND:\n"); */
    /* mesche_value_print(OBJECT_VAL(syntax)); */
    /* printf("\n\n"); */

    // Execute the expander function on top of the existing VM stack, if any
    if (mesche_vm_call_closure(ctx->vm, ctx->vm->expander, 1, (Value[]){syntax}) != INTERPRET_OK) {
      PANIC("FAILED TO EXECUTE EXPANDER\n");
    }

    // TODO: Ensure that we don't need any special GC/stack mechanics here
    // TODO: Verify that we get a syntax value back
    syntax = mesche_vm_stack_pop(ctx->vm);

    /* printf("AFTER EXPAND:\n"); */
    /* mesche_value_print(OBJECT_VAL(syntax)); */
    /* printf("\n\n"); */
  }

  // At this point, the current expression can be:
  // - A syntax containing a list (cons)
  // - A syntax containing a value or other object
  // - An empty list value
  // Most types are literals and should be stored as constants
  Value value = AS_SYNTAX(syntax)->value;
  if (IS_NUMBER(value) || IS_CHAR(value) || IS_STRING(value) || IS_KEYWORD(value)) {
    compiler_emit_constant(ctx, syntax, value);
  } else if (IS_FALSE(value) || IS_TRUE(value) || IS_EMPTY(value)) {
    compiler_emit_literal(ctx, syntax);
  } else if (IS_SYMBOL(value)) {
    return compiler_emit_identifier(ctx, syntax);
  } else if (IS_CONS(value)) {
    return compiler_parse_list(ctx, syntax);
  } else {
    // TODO: Write an error
    // TODO: Use the syntax object directly
    if (IS_OBJECT(value)) {
      PANIC("Unexpected expression object: %d\n", AS_OBJECT(value)->kind);
    } else {
      PANIC("Unexpected expression value: %d\n", value.kind);
    }
  }
}

Value compile_expr(CompilerContext *ctx, Value expr) {
  // Ensure that the expression is a syntax
  if (!IS_SYNTAX(expr)) {
    PANIC("Received something other than a syntax!")
  }

  // Parse the expression
  OK(compiler_parse_expr(ctx, expr));

  // Retrieve the final function
  return compiler_end(ctx);
}

Value compile_all(CompilerContext *ctx, Reader *reader) {
  // Read all expressions and compile them individually
  // TODO: Catch and report errors!
  bool pop_previous = false;
  for (;;) {
    // Read the next expression and push it to the stack to avoid GC
    Value next_expr = mesche_reader_read_next(reader);
    mesche_vm_stack_push(ctx->vm, next_expr);

    // Ensure that the expression is a syntax
    if (!IS_SYNTAX(next_expr)) {
      PANIC("Received something other than a syntax!")
    }

    /* PRINT_VALUE(ctx->vm, "-- COMPILE EXPR: ", next_expr); */

    if (!IS_EOF(AS_SYNTAX(next_expr)->value)) {
      // Should we pop the previous result before compiling the next expression?
      if (pop_previous) {
        // When reading all expressions in a file, pop the intermediate results
        compiler_emit_byte(ctx, next_expr, OP_POP);
      }

      // Parse the expression
      OK(compiler_parse_expr(ctx, next_expr));

      // Pop the result previous result before the next expression
      pop_previous = true;

      mesche_vm_stack_pop(ctx->vm);
    } else {
      break;
    }
  }

  // Pop the last syntax from the stack
  mesche_vm_stack_pop(ctx->vm);

  // Retrieve the final function
  return compiler_end(ctx);
}

Value mesche_compile_source(VM *vm, Reader *reader) {
  // Set up the context
  CompilerContext ctx = {.vm = vm, .file_name = reader->file_name};

  // Set up the compilation context
  compiler_init_context(&ctx, NULL, TYPE_SCRIPT, NULL);

  // Compile the whole source
  Value result = compile_all(&ctx, reader);

  // Clear the VM's pointer to this compiler
  vm->current_compiler = NULL;

  return result;
}

Value mesche_compile_module(VM *vm, ObjectModule *module, Reader *reader) {
  // Set up the context
  CompilerContext ctx = {.vm = vm, .file_name = reader->file_name};

  // Set up the compilation context
  compiler_init_context(&ctx, NULL, TYPE_SCRIPT, module);

  // Compile the module contents
  Value compile_result = compile_all(&ctx, reader);
  OK(compile_result);
  ObjectFunction *function = AS_FUNCTION(compile_result);

  // Ensure the user defined a module in the file
  // TODO: How can we tell if the name is already set?
  if (ctx.module->name == NULL) {
    return compiler_error(&ctx, FALSE_VAL,
                          "A valid module definition was not found in the source file");
  }

  // Assign the function as the module's top-level body
  ctx.module->init_function = function;

  // Clear the VM's pointer to this compiler
  vm->current_compiler = NULL;

  return OBJECT_VAL(ctx.module);
}

Value compiler_compile_msc(VM *vm, int arg_count, Value *args) {
  ObjectSyntax *syntax = NULL;
  EXPECT_ARG_COUNT(1);
  EXPECT_OBJECT_KIND(ObjectKindSyntax, 0, AS_SYNTAX, syntax);

  // Set up the context
  CompilerContext ctx = {.vm = vm};

  // Set up the compilation context
  compiler_init_context(&ctx, NULL, TYPE_SCRIPT, vm->current_module);

  // Compile the expression
  Value result = compile_expr(&ctx, OBJECT_VAL(syntax));

  // Clear the VM's pointer to this compiler
  vm->current_compiler = NULL;

  return result;
}

void mesche_compiler_module_init(VM *vm) {
  mesche_vm_define_native_funcs(
      vm, "mesche compiler",
      (MescheNativeFuncDetails[]){{"compile", compiler_compile_msc, true}, {NULL, NULL, false}});
}
