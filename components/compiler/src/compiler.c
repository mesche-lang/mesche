#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "compiler.h"
#include "disasm.h"
#include "gc.h"
#include "keyword.h"
#include "mem.h"
#include "module.h"
#include "object.h"
#include "op.h"
#include "reader.h"
#include "string.h"
#include "syntax.h"
#include "util.h"
#include "vm-impl.h"

#define UINT8_COUNT (UINT8_MAX + 1)
#define MAX_AND_OR_EXPRS 100

// NOTE: Enable this for diagnostic purposes
/* #define DEBUG_PRINT_CODE */

#define CHECK_VALUE(val, knd) (IS_SYNTAX(val) && val.kind == knd) || val.kind == knd

#define CHECK_OBJECT(val, knd)                                                                     \
  (IS_SYNTAX(val) && IS_OBJECT(AS_SYNTAX(val)->value) &&                                           \
   OBJECT_KIND(AS_SYNTAX(val)->value) == knd) ||                                                   \
      (IS_OBJECT(val) && OBJECT_KIND(val) == knd)

#define CHECK_EMPTY(val) CHECK_VALUE(val, VALUE_EMPTY)
#define CHECK_CONS(val) CHECK_OBJECT(val, ObjectKindCons)
#define CHECK_SYMBOL(val) CHECK_OBJECT(val, ObjectKindSymbol)

#define EXPECT_OBJECT(is_macro, as_macro, src_var, result_var, description)                        \
  if (IS_SYNTAX(src_var) && is_macro(AS_SYNTAX(src_var)->value)) {                                 \
    result_var = as_macro(AS_SYNTAX(src_var)->value);                                              \
  } else {                                                                                         \
    ObjectSyntax *_syntax = AS_SYNTAX(src_var);                                                    \
    PANIC("Expected %s at line %d, column %d in %s.", description, _syntax->line, _syntax->column, \
          _syntax->file_name ? _syntax->file_name->chars : "(unknown)");                           \
  }

#define EXPECT_CONS(src_var, result_var)                                                           \
  EXPECT_OBJECT(IS_CONS, AS_CONS, src_var, result_var, "list")

#define EXPECT_SYMBOL(src_var, result_var)                                                         \
  EXPECT_OBJECT(IS_SYMBOL, AS_SYMBOL, src_var, result_var, "symbol")

#define MAYBE_OBJECT(is_macro, as_macro, src_var, result_var)                                      \
  if (IS_SYNTAX(src_var) && is_macro(AS_SYNTAX(src_var)->value)) {                                 \
    result_var = as_macro(AS_SYNTAX(src_var)->value);                                              \
  } else {                                                                                         \
    result_var = NULL;                                                                             \
  }

#define MAYBE_CONS(src_var, result_var) MAYBE_OBJECT(IS_CONS, AS_CONS, src_var, result_var)
#define MAYBE_STRING(src_var, result_var) MAYBE_OBJECT(IS_STRING, AS_STRING, src_var, result_var)
#define MAYBE_SYMBOL(src_var, result_var) MAYBE_OBJECT(IS_SYMBOL, AS_SYMBOL, src_var, result_var)
#define MAYBE_KEYWORD(src_var, result_var) MAYBE_OBJECT(IS_KEYWORD, AS_KEYWORD, src_var, result_var)

// Contains context for parsing tokens irrespective of the current compilation
// scope
typedef struct {
  Token current;
  Token previous;
  bool had_error;
  bool panic_mode;
} Parser;

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
  Parser *parser;
  ObjectSyntax *current_syntax;
  ObjectString *current_file;

  // `module` should only be set when parsing a module body!  Sub-functions
  // should not have the same module set.
  ObjectModule *module; // NOTE: Might be null if not compiling module
  ObjectFunction *function;
  FunctionType function_type;

  Local locals[UINT8_COUNT];
  int local_count;
  int scope_depth;
  Upvalue upvalues[UINT8_COUNT];

  int tail_site_count;
  uint8_t tail_sites[UINT8_COUNT];
} CompilerContext;

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
    ctx->parser = parent->parser;
    ctx->current_syntax = parent->current_syntax;
  }

  // Set up the compiler state for this scope
  ctx->function = mesche_object_make_function(ctx->vm, type);
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

static void compiler_emit_byte(CompilerContext *ctx, uint8_t byte) {
  // TODO: How do I get the line/col here?
  ctx->function->chunk.file_name = ctx->current_file;
  mesche_chunk_write(ctx->mem, &ctx->function->chunk, byte, ctx->current_syntax->line);
}

static void compiler_emit_bytes(CompilerContext *ctx, uint8_t byte1, uint8_t byte2) {
  compiler_emit_byte(ctx, byte1);
  compiler_emit_byte(ctx, byte2);
}

static void compiler_emit_call(CompilerContext *ctx, uint8_t arg_count, uint8_t keyword_count) {
  compiler_emit_byte(ctx, OP_CALL);
  compiler_emit_byte(ctx, arg_count);
  compiler_emit_byte(ctx, keyword_count);
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

static void compiler_emit_return(CompilerContext *ctx) { compiler_emit_byte(ctx, OP_RETURN); }

static ObjectFunction *compiler_end(CompilerContext *ctx) {
  ObjectFunction *function = ctx->function;

  if (function->type == TYPE_FUNCTION) {
    // Patch tail calls in the function
    compiler_patch_tail_calls(ctx);
  }

  compiler_emit_return(ctx);

#ifdef DEBUG_PRINT_CODE
  /* if (!ctx->parser->had_error) { */
  mesche_disasm_function(function);
  /* } */
#endif

  return function;
}

static uint8_t compiler_make_constant(CompilerContext *ctx, Value value) {
  int constant = mesche_chunk_constant_add(ctx->mem, &ctx->function->chunk, value);

  // TODO: We'll want to make sure we have at least 16 bits for constants
  if (constant > UINT8_MAX) {
    PANIC("Too many constants in one chunk!\n");
  }

  return (uint8_t)constant;
}

static void compiler_emit_constant(CompilerContext *ctx, Value syntax) {
  compiler_emit_bytes(ctx, OP_CONSTANT,
                      compiler_make_constant(ctx, mesche_syntax_to_datum(ctx->vm, syntax)));
}

static void compiler_error_at_token(CompilerContext *ctx, Token *token, const char *message) {
  // If we're already in panic mode, ignore errors to avoid spam
  if (ctx->parser->panic_mode)
    return;

  // Turn on panic mode until we resynchronize
  ctx->parser->panic_mode = true;

  printf("[line %d] Error", token->line);
  if (token->kind == TokenKindEOF) {
    printf(" at end");
  } else if (token->kind == TokenKindError) {
    // Already an error
  } else {
    printf(" at '%.*s'", token->length, token->start);
  }

  printf(":  %s\n", message);

  ctx->parser->had_error = true;
}

static void compiler_error(CompilerContext *ctx, const char *message) {
  compiler_error_at_token(ctx, &ctx->parser->previous, message);
}

static void compiler_error_at_current(CompilerContext *ctx, const char *message) {
  compiler_error_at_token(ctx, &ctx->parser->current, message);
}

// Predefine the main parser function
static void compiler_parse_expr(CompilerContext *ctx, Value value);

static void compiler_parse_literal(CompilerContext *ctx, Value syntax) {
  Value value = AS_SYNTAX(syntax)->value;
  if (IS_TRUE(value)) {
    compiler_emit_byte(ctx, OP_TRUE);
  } else if (IS_FALSE(value)) {
    compiler_emit_byte(ctx, OP_FALSE);
  } else if (IS_EMPTY(value)) {
    compiler_emit_byte(ctx, OP_EMPTY);
  } else {
    // TODO: Is an error really needed here?
  }
}

static void compiler_parse_block(CompilerContext *ctx, Value syntax) {
  int previous_tail_count = ctx->tail_site_count;

  ObjectCons *list;
  EXPECT_CONS(syntax, list);

  for (;;) {
    // Reset the tail sites to log again for the sub-expression
    ctx->tail_site_count = previous_tail_count;

    // Parse the sub-expression
    compiler_parse_expr(ctx, list->car);

    // Are we finished parsing expressions?
    if (IS_EMPTY(list->cdr)) {
      // Log the possible tail call if there should be one
      compiler_log_tail_site(ctx);
      break;
    } else {
      // If we continue the loop, pop the last expression result
      compiler_emit_byte(ctx, OP_POP);
      EXPECT_CONS(list->cdr, list);
    }
  }
}

static void compiler_add_local(CompilerContext *ctx, ObjectSymbol *symbol) {
  if (ctx->local_count == UINT8_COUNT) {
    compiler_error(ctx, "Too many local variables defined in function.");
  }

  Local *local = &ctx->locals[ctx->local_count++];
  local->name = OBJECT_VAL(symbol->name);
  local->depth = -1; // The variable is uninitialized until assigned
  local->is_captured = false;
}

static int compiler_resolve_local(CompilerContext *ctx, ObjectSymbol *symbol) {
  // Find the identifier by name in scope chain
  for (int i = ctx->local_count - 1; i >= 0; i--) {
    Local *local = &ctx->locals[i];
    if (mesche_value_eqv_p(OBJECT_VAL(symbol->name), local->name)) {
      if (local->depth == -1) {
        compiler_error(ctx, "Referenced variable before it was bound");
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

static int compiler_add_upvalue(CompilerContext *ctx, uint8_t index, bool is_local) {
  // Can we reuse an existing upvalue?
  int upvalue_count = ctx->function->upvalue_count;
  for (int i = 0; i < upvalue_count; i++) {
    Upvalue *upvalue = &ctx->upvalues[i];
    if (upvalue->index == index && upvalue->is_local == is_local) {
      return i;
    }
  }

  if (upvalue_count == UINT8_COUNT) {
    compiler_error(ctx, "Reached the limit of closures captured in one function.");
  }

  // Initialize the next upvalue slot
  ctx->upvalues[upvalue_count].is_local = is_local;
  ctx->upvalues[upvalue_count].index = index;
  return ctx->function->upvalue_count++;
}

static int compiler_resolve_upvalue(CompilerContext *ctx, ObjectSymbol *symbol) {
  // If there's no parent context then there's nothing to close over
  if (ctx->parent == NULL)
    return -1;

  // First try to resolve the variable as a local in the parent
  int local = compiler_resolve_local(ctx->parent, symbol);
  if (local != -1) {
    ctx->parent->locals[local].is_captured = true;
    return compiler_add_upvalue(ctx, (uint8_t)local, true);
  }

  // If we didn't find a local, look for a binding from a parent scope
  int upvalue = compiler_resolve_upvalue(ctx->parent, symbol);
  if (upvalue != -1) {
    return compiler_add_upvalue(ctx, (uint8_t)upvalue, false);
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

static void compiler_declare_variable(CompilerContext *ctx, ObjectSymbol *symbol) {
  // No need to declare a global, it will be dynamically resolved
  if (ctx->scope_depth == 0)
    return;

  // Start from the most recent local and work backwards to
  // find an existing variable with the same binding in this scope
  for (int i = ctx->local_count - 1; i >= 0; i--) {
    // If the local is in a different scope, stop looking
    Local *local = &ctx->locals[i];
    if (local->depth != -1 && local->depth < ctx->scope_depth)
      break;

    // In the same scope, is the identifier the same?
    if (mesche_value_eqv_p(OBJECT_VAL(symbol->name), local->name)) {
      compiler_error(ctx, "Duplicate variable binding in 'let'");
      return;
    }
  }

  // Add a local binding for the name
  compiler_add_local(ctx, symbol);
}

static uint8_t compiler_parse_symbol(CompilerContext *ctx, ObjectSymbol *symbol, bool is_global) {
  // Declare the variable and exit if we're in a local scope
  if (!is_global && ctx->scope_depth > 0) {
    compiler_declare_variable(ctx, symbol);
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

static void compiler_parse_identifier(CompilerContext *ctx, Value syntax) {
  ObjectSymbol *symbol;
  EXPECT_SYMBOL(syntax, symbol);

  // Are we looking at a local variable?
  // TODO: Leverage lexical information in the syntax object
  int local_index = compiler_resolve_local(ctx, symbol);
  if (local_index != -1) {
    compiler_emit_bytes(ctx, OP_READ_LOCAL, (uint8_t)local_index);
  } else if ((local_index = compiler_resolve_upvalue(ctx, symbol)) != -1) {
    // Found an upvalue
    compiler_emit_bytes(ctx, OP_READ_UPVALUE, (uint8_t)local_index);
  } else {
    uint8_t variable_constant = compiler_parse_symbol(ctx, symbol, true);
    compiler_emit_bytes(ctx, OP_READ_GLOBAL, variable_constant);
  }
}

static uint8_t compiler_define_variable_ex(CompilerContext *ctx, uint8_t variable_constant,
                                           DefineAttributes *define_attributes) {
  // We don't define global variables in local scopes
  if (ctx->scope_depth > 0) {
    compiler_local_mark_initialized(ctx);
    return ctx->local_count - 1;
  }

  compiler_emit_bytes(ctx, OP_DEFINE_GLOBAL, variable_constant);

  if (define_attributes) {
    if (define_attributes->is_export) {
      // Add the binding to the module
      if (ctx->module != NULL) {
        mesche_value_array_write((MescheMemory *)ctx->vm, &ctx->module->exports,
                                 ctx->function->chunk.constants.values[variable_constant]);
      }

      // TODO: Convert to OP_CREATE_BINDING?
      /* compiler_emit_bytes(ctx, OP_EXPORT_SYMBOL, variable_constant); */
    }
  }

  // TODO: What does this really mean?
  return 0;
}

static uint8_t compiler_define_variable(CompilerContext *ctx, uint8_t variable_constant) {
  return compiler_define_variable_ex(ctx, variable_constant, NULL);
}

static void compiler_parse_set(CompilerContext *ctx, Value syntax) {
  ObjectCons *list;
  EXPECT_CONS(syntax, list);

  // The first list item must be a symbol
  ObjectSymbol *symbol;
  EXPECT_SYMBOL(list->car, symbol);

  uint8_t instr = OP_SET_LOCAL;
  int arg = compiler_resolve_local(ctx, symbol);

  // If there isn't a local, use a global variable instead
  if (arg != -1) {
    // Do nothing, all values are already set.
  } else if ((arg = compiler_resolve_upvalue(ctx, symbol)) != -1) {
    instr = OP_SET_UPVALUE;
  } else {
    arg = compiler_parse_symbol(ctx, symbol, true);
    instr = OP_SET_GLOBAL;
  }

  // Parse the value to set
  EXPECT_CONS(list->cdr, list);
  compiler_parse_expr(ctx, list->car);
  compiler_emit_bytes(ctx, instr, arg);

  /* compiler_consume(ctx, TokenKindRightParen, "Expected closing paren."); */
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
      compiler_emit_byte(ctx, OP_CLOSE_UPVALUE);
    } else {
      compiler_emit_bytes(ctx, OP_POP_SCOPE, 1);
    }
    ctx->local_count--;
  }
}

static void compiler_parse_let(CompilerContext *ctx, Value syntax) {
  int previous_tail_count = ctx->tail_site_count;

  ObjectCons *list;
  EXPECT_CONS(syntax, list);

  // Create a new compiler context for parsing the let body as an inline
  // function
  CompilerContext let_ctx;
  compiler_init_context(&let_ctx, ctx, TYPE_FUNCTION, NULL);
  compiler_begin_scope(&let_ctx);

  // Parse the name of the let, if any
  ObjectSymbol *let_name;
  MAYBE_SYMBOL(list->car, let_name);
  if (let_name) {
    // Use the symbol name as the let function's name
    let_ctx.function->name = let_name->name;
    EXPECT_CONS(list->cdr, list);
  }

  // Before parsing arguments, skip ahead to leave enough space to write out the
  // OP_CLOSURE instruction with the right constant value before writing out the
  // OP_CALL instruction.
  int func_offset = ctx->function->chunk.count;
  ctx->function->chunk.count += 2;

  // Process the binding list
  Value bindings = list->car;
  if (!IS_SYNTAX(bindings) || !IS_EMPTY(AS_SYNTAX(bindings)->value)) {
    while (!IS_EMPTY(bindings)) {
      ObjectCons *binding_list;
      EXPECT_CONS(bindings, binding_list);

      // Increase the binding (function argument) count
      let_ctx.function->arity++;
      if (let_ctx.function->arity > 255) {
        compiler_error_at_current(&let_ctx, "Let cannot have more than 255 bindings.");
      }

      // Parse the symbol for the binding
      // TODO: Support binding without value?
      ObjectCons *binding;
      EXPECT_CONS(binding_list->car, binding);

      ObjectSymbol *symbol;
      EXPECT_SYMBOL(binding->car, symbol);

      uint8_t constant = compiler_parse_symbol(&let_ctx, symbol, false);
      compiler_define_variable(&let_ctx, constant);

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
      EXPECT_CONS(binding->cdr, binding);
      compiler_parse_expr(ctx, binding->car);

      // Pop the function back off the stack and restore the correct context
      mesche_vm_stack_pop(let_ctx.vm);
      let_ctx.vm->current_compiler = &let_ctx;

      // Reset the tail site count so that the binding expression will not be
      // treated as a tail call site
      ctx->tail_site_count = previous_tail_count;

      // Move to the next binding in the list
      bindings = binding_list->cdr;
    }
  }

  // Parse the let body
  compiler_parse_block(&let_ctx, list->cdr);

  // Finish the function and write out its constant and closure back where it
  // needs to go
  ObjectFunction *function = compiler_end(&let_ctx);

  // If the let body function has upvalues, we need to extend the size of the
  // chunk to accomodate the upvalue opcodes
  if (function->upvalue_count > 0) {
    // Ensure the chunk is big enough to insert the upvalue opcodes
    int upvalue_size = function->upvalue_count * 2;
    mesche_chunk_insert_space(ctx->mem, &ctx->function->chunk, func_offset + 2, upvalue_size);
  }

  int call_offset = ctx->function->chunk.count;
  ctx->function->chunk.count = func_offset;
  compiler_emit_bytes(ctx, OP_CLOSURE, compiler_make_constant(ctx, OBJECT_VAL(function)));

  // Write out the references to each upvalue as arguments to OP_CLOSURE
  for (int i = 0; i < function->upvalue_count; i++) {
    compiler_emit_byte(ctx, let_ctx.upvalues[i].is_local ? 1 : 0);
    compiler_emit_byte(ctx, let_ctx.upvalues[i].index);
  }

  // Let the VM know we're back to the parent compiler
  ctx->vm->current_compiler = ctx;

  // Restore the offset where OP_CALL should be and write it
  ctx->function->chunk.count = call_offset;
  compiler_emit_call(ctx, let_ctx.function->arity, 0);
  compiler_log_tail_site(ctx);
}

static Value compiler_parse_define_attributes(CompilerContext *ctx, Value syntax,
                                              DefineAttributes *define_attributes) {
  ObjectCons *list;
  while (!IS_EMPTY(syntax)) {
    // Look for :export keyword
    ObjectKeyword *keyword;
    EXPECT_CONS(syntax, list);
    MAYBE_KEYWORD(list->car, keyword);
    if (keyword) {
      syntax = list->cdr;
      if (memcmp(keyword->string.chars, "export", 7) == 0) {
        define_attributes->is_export = true;
      } else {
        // TODO: Warn on unknown keywords?
      }
    } else {
      break;
    }
  }

  // Look for a docstring
  if (!IS_EMPTY(syntax)) {
    ObjectString *string;
    EXPECT_CONS(syntax, list);
    MAYBE_STRING(list->car, string);
    if (string) {
      // Store it as the documentation string
      define_attributes->doc_string = string;
      syntax = list->cdr;
    }
  }

  return syntax;
}

static bool compiler_compare_keyword(Token keyword, const char *expected_name) {
  return memcmp(keyword.start + 1, expected_name, strlen(expected_name)) == 0;
}

static void compiler_parse_lambda_inner(CompilerContext *ctx, Value formals, Value syntax,
                                        ObjectString *name) {
  ObjectCons *body;
  EXPECT_CONS(syntax, body);

  // Create a new compiler context for parsing the function body
  CompilerContext func_ctx;
  compiler_init_context(&func_ctx, ctx, TYPE_FUNCTION, NULL);
  compiler_begin_scope(&func_ctx);

  ArgType arg_type = ARG_POSITIONAL;

  // Parse the parameter list, one of the following:
  // - A list of symbols
  // - An improper list consed with a symbol
  // - A single symbol as "rest" param
  ObjectSymbol *param;
  ObjectCons *formal_list;
  if (!IS_SYNTAX(formals) || !IS_EMPTY(AS_SYNTAX(formals)->value)) {
    while (!IS_EMPTY(formals)) {
      param = NULL;
      MAYBE_SYMBOL(formals, param);
      MAYBE_CONS(formals, formal_list);

      if (param || formal_list) {
        bool is_rest_param = false;
        if (param) {
          // Now that we've found the rest parameter, exit after processing it
          is_rest_param = true;
          formals = EMPTY_VAL;
        } else {
          EXPECT_SYMBOL(formal_list->car, param)
          formals = formal_list->cdr;
        }

        uint8_t constant = compiler_parse_symbol(&func_ctx, param, false);
        compiler_define_variable(&func_ctx, constant);

        func_ctx.function->arity++;
        if (func_ctx.function->arity > 255) {
          compiler_error_at_current(&func_ctx, "Function cannot have more than 255 parameters.");
        }

        // Store knowledge of the "rest" parameter
        if (is_rest_param) {
          func_ctx.function->rest_arg_index = func_ctx.function->arity;
        }
      }
    }
  }

  // Parse the body (just pass syntax directly)
  compiler_parse_block(&func_ctx, syntax);

  // Get the parsed function and store it in a constant
  func_ctx.function->name = name;
  ObjectFunction *function = compiler_end(&func_ctx);
  compiler_emit_bytes(ctx, OP_CLOSURE, compiler_make_constant(ctx, OBJECT_VAL(function)));

  // Write out the references to each upvalue as arguments to OP_CLOSURE
  for (int i = 0; i < function->upvalue_count; i++) {
    compiler_emit_byte(ctx, func_ctx.upvalues[i].is_local ? 1 : 0);
    compiler_emit_byte(ctx, func_ctx.upvalues[i].index);
  }

  // Let the VM know we're back to the parent compiler
  ctx->vm->current_compiler = ctx;
}

static void compiler_parse_lambda(CompilerContext *ctx, Value syntax) {
  ObjectCons *list;
  EXPECT_CONS(syntax, list);
  compiler_parse_lambda_inner(ctx, list->car, list->cdr, NULL);
}

static void compiler_parse_apply(CompilerContext *ctx, Value syntax) {
  // Apply is very simple, it just takes two expressions, one for the function and the other
  // for the list.
  ObjectCons *list;
  EXPECT_CONS(syntax, list);

  // TODO: Might need to unwrap cdr better
  Value callee = list->car;
  EXPECT_CONS(list->cdr, list)

  compiler_parse_expr(ctx, callee);
  compiler_parse_expr(ctx, list->car);
  compiler_emit_byte(ctx, OP_APPLY);
}

static void compiler_parse_reset(CompilerContext *ctx, Value syntax) {
  // Unwrap the expression to reach the inner lambda
  ObjectCons *list;
  EXPECT_CONS(syntax, list);
  EXPECT_CONS(list->car, list);

  // `reset` requires a lambda expression with no arguments
  ObjectSymbol *symbol;
  EXPECT_SYMBOL(list->car, symbol);
  /* compiler_error(ctx, "Expected lambda expression after 'reset'."); */

  // Parse the lambda body of the reset expression *after* emitting OP_RESET so
  // that we can set the new reset context before adding the closure to the
  // stack.
  compiler_emit_byte(ctx, OP_RESET);
  compiler_parse_lambda(ctx, list->cdr);
  compiler_emit_call(ctx, 0, 0);
  compiler_emit_byte(ctx, OP_NOP); // Avoid turning this into a tail call!
}

static void compiler_parse_shift(CompilerContext *ctx, Value syntax) {
  // Unwrap the expression to reach the inner lambda
  ObjectCons *list;
  EXPECT_CONS(syntax, list);
  EXPECT_CONS(list->car, list);

  // `shift` requires a lambda expression with one argument
  ObjectSymbol *symbol;
  EXPECT_SYMBOL(list->car, symbol);
  /* compiler_error(ctx, "Expected lambda expression after 'shift'."); */

  // Parse the lambda body of the reset expression *before* emitting OP_SHIFT so
  // that the shift body will be invoked with the continuation function as its
  // parameter.
  compiler_parse_lambda(ctx, list->cdr);
  compiler_emit_byte(ctx, OP_SHIFT);
  compiler_emit_call(ctx, 1, 0);
  compiler_emit_byte(ctx, OP_NOP); // Avoid turning this into a tail call!
}

static void compiler_parse_define(CompilerContext *ctx, Value syntax) {
  DefineAttributes define_attributes;
  define_attributes.is_export = false;
  define_attributes.doc_string = NULL;

  // TODO: Only allow defines at the top of let/lambda bodies
  // TODO: Check ctx for this!

  ObjectCons *list;
  EXPECT_CONS(syntax, list);

  // The next expression should either be a symbol or an open paren to define a
  // function
  ObjectSymbol *symbol = NULL;
  Value formals = FALSE_VAL;
  Value body = EMPTY_VAL;
  MAYBE_SYMBOL(list->car, symbol);
  if (symbol) {
    body = list->cdr;
  } else {
    ObjectCons *formals_list;
    EXPECT_CONS(list->car, formals_list);
    EXPECT_SYMBOL(formals_list->car, symbol);
    formals = formals_list->cdr;
    body = list->cdr;
  }

  uint8_t variable_constant = compiler_parse_symbol(ctx, symbol, true);
  if (!IS_FALSE(formals)) {
    // Handle define attributes and continue parsing the lambda body where that
    // leaves off
    body = compiler_parse_define_attributes(ctx, body, &define_attributes);
    compiler_parse_lambda_inner(ctx, formals, body, symbol->name);
  } else {
    // Unwrap the body first
    EXPECT_CONS(body, list);
    body = list->car;

    // Parse the value expression for the binding
    compiler_parse_expr(ctx, body);

    // Only consider attributes if there's something else in the `define` list
    if (!IS_EMPTY(list->cdr)) {
      compiler_parse_define_attributes(ctx, list->cdr, &define_attributes);
    }
  }

  compiler_define_variable_ex(ctx, variable_constant, &define_attributes);
}

static void compiler_parse_module_name(CompilerContext *ctx, Value syntax) {
  char *module_name = NULL;
  uint8_t symbol_count = 0;

  // Step in and get access to the real module name symbol list
  ObjectCons *cons;
  EXPECT_CONS(syntax, cons);
  syntax = cons->car;

  while (!IS_EMPTY(syntax)) {
    EXPECT_CONS(syntax, cons);

    ObjectSymbol *symbol;
    MAYBE_SYMBOL(cons->car, symbol);
    if (symbol) {
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
      syntax = cons->cdr;
      symbol_count++;
    } else {
      /* compiler_consume(ctx, TokenKindSymbol, "Module names can only be comprised of
  symbols."); */
    }
  }

  if (module_name != NULL) {
    // If a module name was parsed, emit the constant module name string
    ObjectString *module_name_str =
        mesche_object_make_string(ctx->vm, module_name, strlen(module_name));
    compiler_emit_constant(ctx, OBJECT_VAL(module_name_str));

    // The module name has been copied, so free the temporary string
    free(module_name);
  } else {
    compiler_error(ctx, "No symbols were found where module name was expected.");
  }
}

static void compiler_parse_module_import(CompilerContext *ctx, Value syntax) {
  compiler_parse_module_name(ctx, syntax);
  compiler_emit_byte(ctx, OP_IMPORT_MODULE);
}

static void compiler_parse_module_enter(CompilerContext *ctx, Value syntax) {
  compiler_parse_module_name(ctx, syntax);
  compiler_emit_byte(ctx, OP_ENTER_MODULE);
}

static void compiler_parse_define_module(CompilerContext *ctx, Value syntax) {
  ObjectCons *list;
  EXPECT_CONS(syntax, list);

  // Read the symbol list for the module path
  compiler_parse_module_name(ctx, syntax);
  compiler_emit_byte(ctx, OP_DEFINE_MODULE);

  // Store the module name if we're currently parsing a module file
  if (ctx->module) {
    ctx->module->name =
        AS_STRING(ctx->function->chunk.constants.values[ctx->function->chunk.constants.count - 1]);
  }

  // Check for a possible 'import' expression
  Value imports = list->cdr;
  if (!IS_EMPTY(imports)) {
    // Unwrap the imports list (effectively a cadr)
    EXPECT_CONS(list->cdr, list);
    EXPECT_CONS(list->car, list);

    // TODO: ERROR
    /* compiler_consume_sub(ctx, TokenKindImport, "Expected 'import' inside of
'define-module'."); */

    // Verify that the first expression is the symbol `import`
    ObjectSymbol *symbol;
    EXPECT_SYMBOL(list->car, symbol);
    if (symbol->token_kind != TokenKindImport) {
      // TODO: Error
      printf("Expected 'import' inside of 'define-module'.");
      return;
    }

    // There can be multiple import specifications
    imports = list->cdr;
    while (!IS_EMPTY(imports)) {
      /* compiler_parse_module_name(ctx, import_list->car); */
      EXPECT_CONS(imports, list)
      compiler_parse_module_import(ctx, imports);
      compiler_emit_byte(ctx, OP_POP);
      imports = list->cdr;
    }
  }
}

// TODO: This should be replaced with a syntax-case
static void compiler_parse_define_record_type(CompilerContext *ctx, Value syntax) {
  ObjectCons *list;
  EXPECT_CONS(syntax, list);

  // Parse and emit the name symbol
  ObjectSymbol *record_name;
  EXPECT_SYMBOL(list->car, record_name);
  compiler_emit_constant(ctx, list->car);

  // Find the fields list (effectively a cadr)
  EXPECT_CONS(list->cdr, list)
  EXPECT_CONS(list->car, list);

  ObjectSymbol *fields_symbol;
  EXPECT_SYMBOL(list->car, fields_symbol)
  uint8_t field_count = 0;
  if (fields_symbol && memcmp(fields_symbol->name->chars, "fields", 6) == 0) {
    // Read the key-value pairs inside of the form
    Value fields = list->cdr;
    while (!IS_EMPTY(fields)) {
      ObjectSymbol *field_name;
      EXPECT_CONS(fields, list);
      EXPECT_SYMBOL(list->car, field_name);
      if (field_name) {
        compiler_emit_constant(ctx, list->car);
        compiler_emit_constant(ctx, FALSE_VAL);
        field_count++;
      }

      fields = list->cdr;
    }
  } else {
    PANIC("Expected 'fields' after 'define-record-type'.");
    return;
  }

  compiler_emit_bytes(ctx, OP_DEFINE_RECORD, field_count);
}

static int compiler_emit_jump(CompilerContext *ctx, uint8_t instruction) {
  // Write out the two bytes that will be patched once the jump target location
  // is determined
  compiler_emit_byte(ctx, instruction);
  compiler_emit_byte(ctx, 0xff);
  compiler_emit_byte(ctx, 0xff);

  return ctx->function->chunk.count - 2;
}

static void compiler_patch_jump(CompilerContext *ctx, int offset) {
  // We offset by -2 to adjust for the size of the jump offset itself
  int jump = ctx->function->chunk.count - offset - 2;

  if (jump > UINT16_MAX) {
    compiler_error(ctx, "Attempting to emit jump that is larger than possible jump size.");
  }

  // Place the two bytes with the jump delta
  ctx->function->chunk.code[offset] = (jump >> 8) & 0xff;
  ctx->function->chunk.code[offset + 1] = jump & 0xff;
}

static void compiler_parse_if(CompilerContext *ctx, Value syntax) {
  int previous_tail_count = ctx->tail_site_count;

  ObjectCons *cons;
  EXPECT_CONS(syntax, cons);

  // Parse predicate and write out a jump instruction to the else code
  // path if the predicate evaluates to false
  compiler_parse_expr(ctx, cons->car);
  int jump_origin = compiler_emit_jump(ctx, OP_JUMP_IF_FALSE);

  // Restore the tail count because the predicate expression should never
  // produce a tail call
  ctx->tail_site_count = previous_tail_count;

  // Include a pop so that the expression value gets removed from the stack in
  // the truth path
  compiler_emit_byte(ctx, OP_POP);

  // Parse truth expr and log the tail call if the expression was still in a
  // tail context afterward
  EXPECT_CONS(cons->cdr, cons);
  compiler_parse_expr(ctx, cons->car);
  compiler_log_tail_site(ctx);

  // Write out a jump from the end of the truth case to the end of the expression
  int else_jump = compiler_emit_jump(ctx, OP_JUMP);

  // Patch the jump instruction which leads to the else path
  compiler_patch_jump(ctx, jump_origin);

  // Include a pop so that the predicate expression value gets removed from the stack
  compiler_emit_byte(ctx, OP_POP);

  // Is there an else expression?
  if (!IS_EMPTY(cons->cdr)) {
    EXPECT_CONS(cons->cdr, cons);
    compiler_parse_expr(ctx, cons->car);
    compiler_log_tail_site(ctx);
  } else {
    // Push a '#f' onto the value stack if there was no else
    compiler_emit_byte(ctx, OP_FALSE);
  }

  // Patch the jump instruction after the false path has been compiled
  compiler_patch_jump(ctx, else_jump);
}

static void compiler_parse_or(CompilerContext *ctx, Value syntax) {
  int prev_jump = -1;
  int expr_count = 0;
  int end_jumps[MAX_AND_OR_EXPRS];

  ObjectCons *list;
  Value exprs = syntax;
  while (!IS_EMPTY(exprs)) {
    if (expr_count > 0) {
      // If it evaluates to false, jump to the next expression
      prev_jump = compiler_emit_jump(ctx, OP_JUMP_IF_FALSE);

      // If it evaluates to true, jump to the end of the expression list
      end_jumps[expr_count - 1] = compiler_emit_jump(ctx, OP_JUMP);

      // Patch the previous expression's jump
      compiler_patch_jump(ctx, prev_jump);

      // Emit a pop to remove the previous expression value from the stack
      compiler_emit_byte(ctx, OP_POP);
    }

    // Parse the expression
    EXPECT_CONS(exprs, list);
    compiler_parse_expr(ctx, list->car);
    expr_count++;
    exprs = list->cdr;

    if (expr_count == MAX_AND_OR_EXPRS) {
      compiler_error(ctx, "Exceeded the maximum number of expressions in an `and`.");
    }
  }

  // The last expression is a tail site
  compiler_log_tail_site(ctx);

  for (int i = 0; i < expr_count - 1; i++) {
    // Patch all the jumps to the end location
    compiler_patch_jump(ctx, end_jumps[i]);
  }
}

static void compiler_parse_and(CompilerContext *ctx, Value syntax) {
  int prev_jump = -1;
  int expr_count = 0;
  int end_jumps[MAX_AND_OR_EXPRS];

  ObjectCons *list;
  Value exprs = syntax;
  while (!IS_EMPTY(exprs)) {
    if (expr_count > 0) {
      // If it evaluates to false, jump to the end of the expression list
      end_jumps[expr_count - 1] = compiler_emit_jump(ctx, OP_JUMP_IF_FALSE);

      // Emit a pop to remove the previous expression value from the stack
      compiler_emit_byte(ctx, OP_POP);
    }

    // Parse the expression
    EXPECT_CONS(exprs, list);
    compiler_parse_expr(ctx, list->car);
    expr_count++;
    exprs = list->cdr;

    if (expr_count == MAX_AND_OR_EXPRS) {
      compiler_error(ctx, "Exceeded the maximum number of expressions in an `and`.");
    }
  }

  // The last expression is a tail site
  compiler_log_tail_site(ctx);

  for (int i = 0; i < expr_count - 1; i++) {
    // Patch all the jumps to the end location
    compiler_patch_jump(ctx, end_jumps[i]);
  }
}

static bool compiler_parse_operator_call(CompilerContext *ctx, ObjectSymbol *symbol,
                                         uint8_t operand_count) {
  switch (symbol->token_kind) {
  case TokenKindPlus:
    compiler_emit_byte(ctx, OP_ADD);
    break;
  case TokenKindMinus:
    compiler_emit_byte(ctx, OP_SUBTRACT);
    break;
  case TokenKindStar:
    compiler_emit_byte(ctx, OP_MULTIPLY);
    break;
  case TokenKindSlash:
    compiler_emit_byte(ctx, OP_DIVIDE);
    break;
  case TokenKindPercent:
    compiler_emit_byte(ctx, OP_MODULO);
    break;
  case TokenKindNot:
    compiler_emit_byte(ctx, OP_NOT);
    break;
  case TokenKindGreaterThan:
    compiler_emit_byte(ctx, OP_GREATER_THAN);
    break;
  case TokenKindGreaterEqual:
    compiler_emit_byte(ctx, OP_GREATER_EQUAL);
    break;
  case TokenKindLessThan:
    compiler_emit_byte(ctx, OP_LESS_THAN);
    break;
  case TokenKindLessEqual:
    compiler_emit_byte(ctx, OP_LESS_EQUAL);
    break;
  case TokenKindList:
    compiler_emit_bytes(ctx, OP_LIST, operand_count);
    break;
  case TokenKindCons:
    compiler_emit_byte(ctx, OP_CONS);
    break;
  case TokenKindEqv:
    compiler_emit_byte(ctx, OP_EQV);
    break;
  case TokenKindEqual:
    compiler_emit_byte(ctx, OP_EQUAL);
    break;
  case TokenKindDisplay:
    compiler_emit_byte(ctx, OP_DISPLAY);
    break;
  default:
    return false;
  }

  return true;
}

static void compiler_parse_load_file(CompilerContext *ctx, Value syntax) {
  ObjectCons *cons;
  EXPECT_CONS(syntax, cons);
  compiler_parse_expr(ctx, cons->car);
  compiler_emit_byte(ctx, OP_LOAD_FILE);
}

static void compiler_parse_list(CompilerContext *ctx, Value syntax) {
  // Possibilities
  // - Primitive command with its own opcode
  // - Special form that has non-standard call semantics
  // - Symbol in first position
  // - Raw lambda in first position
  // - Expression that evaluates to lambda
  // In the latter 3 cases, compiler the callee before the arguments

  // Get the pair that starts the list
  ObjectCons *list;
  EXPECT_CONS(syntax, list);

  // Get the expression in the call position
  Value callee = list->car;
  Value args = list->cdr;

  ObjectSymbol *symbol;
  MAYBE_SYMBOL(callee, symbol);
  if (symbol) {
    // Is the first item a symbol that refers to a core syntax or special form?
    switch (symbol->token_kind) {
    case TokenKindOr:
      compiler_parse_or(ctx, args);
      return;
    case TokenKindAnd:
      compiler_parse_and(ctx, args);
      return;
    case TokenKindLambda:
      compiler_parse_lambda(ctx, args);
      return;
    case TokenKindLet:
      compiler_parse_let(ctx, args);
      return;
    case TokenKindDefine:
      compiler_parse_define(ctx, args);
      return;
    case TokenKindSet:
      compiler_parse_set(ctx, args);
      return;
    case TokenKindIf:
      compiler_parse_if(ctx, args);
      return;
    case TokenKindBegin:
      compiler_parse_block(ctx, args);
      return;
    case TokenKindQuote: {
      ObjectCons *cons;
      EXPECT_CONS(args, cons);
      compiler_emit_constant(ctx, cons->car);
      return;
    }
    case TokenKindApply:
      compiler_parse_apply(ctx, args);
      return;
    case TokenKindShift:
      compiler_parse_shift(ctx, args);
      return;
    case TokenKindReset:
      compiler_parse_reset(ctx, args);
      return;
    case TokenKindDefineModule:
      compiler_parse_define_module(ctx, args);
      return;
    case TokenKindModuleImport:
      compiler_parse_module_import(ctx, args);
      return;
    case TokenKindModuleEnter:
      compiler_parse_module_enter(ctx, args);
      return;
    case TokenKindDefineRecordType:
      compiler_parse_define_record_type(ctx, args);
      return;
    case TokenKindLoadFile:
      compiler_parse_load_file(ctx, args);
      return;
    case TokenKindBreak:
      // Just emit OP_BREAK and be done
      compiler_emit_byte(ctx, OP_BREAK);
      return;
    }
  }

  // If we didn't see an operator, evaluate the callee expression to get the
  // procedure we need to invoke
  bool is_operator = symbol && symbol->token_kind != TokenKindNone;
  if (!is_operator) {
    compiler_parse_expr(ctx, callee);
  }

  // Parse argument expressions until we reach a right paren
  uint8_t arg_count = 0;
  while (!IS_EMPTY(args)) {
    ObjectCons *cons;
    EXPECT_CONS(args, cons);

    // Compile next positional parameter
    compiler_parse_expr(ctx, cons->car);
    args = cons->cdr;

    arg_count++;
    if (arg_count == 255) {
      compiler_error(ctx, "Cannot pass more than 255 arguments in a function call.");
    }
  }

  // Finally, emit the OP_CALL if we're invoking a procedure
  if (is_operator) {
    // Emit the operator call
    is_operator = compiler_parse_operator_call(ctx, symbol, arg_count);
  }

  // One last chance to decide that this should be an OP_CALL
  if (!is_operator) {
    compiler_emit_call(ctx, arg_count, 0);
  }
}

static void compiler_parse_syntax(CompilerContext *ctx, ObjectSyntax *syntax) {
  // Try expanding the expression before parsing it.  This enables syntax
  // transformers which return a single value like `(and 3)`.
  if (ctx->vm->expander && IS_CONS(syntax->value)) {
    /* printf("BEFORE EXPAND:\n"); */
    /* mesche_value_print(OBJECT_VAL(syntax)); */
    /* printf("\n\n"); */

    // Execute the expander function on top of the existing VM stack, if any
    if (mesche_vm_call_closure(ctx->vm, ctx->vm->expander, 1, (Value[]){OBJECT_VAL(syntax)}) !=
        INTERPRET_OK) {
      PANIC("FAILED TO EXECUTE EXPANDER\n");
    }

    // TODO: Ensure that we don't need any special GC/stack mechanics here
    // TODO: Verify that we get a syntax value back
    syntax = AS_SYNTAX(mesche_vm_stack_pop(ctx->vm));

    /* printf("AFTER EXPAND:\n"); */
    /* mesche_value_print(OBJECT_VAL(syntax)); */
    /* printf("\n\n"); */
  }

  // Most types are literals and should be stored as constants
  Value value = syntax->value;
  if (IS_NUMBER(value) || IS_STRING(value) || IS_KEYWORD(value)) {
    compiler_emit_constant(ctx, OBJECT_VAL(syntax));
  } else if (IS_FALSE(value) || IS_TRUE(value) || IS_EMPTY(value)) {
    compiler_parse_literal(ctx, OBJECT_VAL(syntax));
  } else if (IS_SYMBOL(value)) {
    compiler_parse_identifier(ctx, OBJECT_VAL(syntax));
  } else if (IS_CONS(value)) {
    compiler_parse_list(ctx, OBJECT_VAL(syntax));
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

// TODO: Eventually get rid of this or merge with compiler_parse_syntax
static void compiler_parse_expr(CompilerContext *ctx, Value expr) {
  // Ensure that the expression is a syntax
  if (IS_SYNTAX(expr)) {
    compiler_parse_syntax(ctx, AS_SYNTAX(expr));
  } else {
    PANIC("Received something other than a syntax!")
  }
}

ObjectFunction *compile_all(CompilerContext *ctx, Reader *reader) {
  // Read all expressions and compile them individually
  // TODO: Catch and report errors!
  bool pop_previous = false;
  for (;;) {
    // Read the next expression
    ObjectSyntax *next_expr = mesche_reader_read_next(reader);
    mesche_vm_stack_push(ctx->vm, OBJECT_VAL(next_expr));

    // TODO: This is temporary to make it so we can get a reasonable
    // triangulation for line numbers
    ctx->current_syntax = next_expr;
    ctx->current_file = next_expr->file_name;

    /* PRINT_VALUE("EXPR: ", OBJECT_VAL(next_expr)); */

    if (!IS_EOF(next_expr->value)) {
      // Should we pop the previous result before compiling the next expression?
      if (pop_previous) {
        // When reading all expressions in a file, pop the intermediate results
        compiler_emit_byte(ctx, OP_POP);
      }

      // Parse the expression
      compiler_parse_syntax(ctx, next_expr);

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

ObjectFunction *mesche_compile_source(VM *vm, Reader *reader) {
  // Set up the context
  CompilerContext ctx = {.vm = vm};

  // Set up the compilation context
  compiler_init_context(&ctx, NULL, TYPE_SCRIPT, NULL);

  // Compile the whole source
  ObjectFunction *function = compile_all(&ctx, reader);

  // Clear the VM's pointer to this compiler
  vm->current_compiler = NULL;

  // TODO: What kind of errors can occur?
  return function;
}

ObjectModule *mesche_compile_module(VM *vm, ObjectModule *module, Reader *reader) {
  // Set up the context
  CompilerContext ctx = {.vm = vm};

  // Set up the compilation context
  compiler_init_context(&ctx, NULL, TYPE_SCRIPT, module);

  // Compile the whole source
  ObjectFunction *function = compile_all(&ctx, reader);

  // Ensure the user defined a module in the file
  // TODO: How can we tell if the name is already set?
  if (ctx.module->name == NULL) {
    compiler_error(&ctx, "A valid module definition was not found in the source file.");
  }

  // Assign the function as the module's top-level body
  ctx.module->init_function = function;

  // Clear the VM's pointer to this compiler
  vm->current_compiler = NULL;

  return ctx.module;
}
