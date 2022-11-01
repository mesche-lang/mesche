#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "compiler.h"
#include "disasm.h"
#include "mem.h"
#include "object.h"
#include "op.h"
#include "scanner.h"
#include "string.h"
#include "util.h"
#include "vm.h"

#define UINT8_COUNT (UINT8_MAX + 1)
#define MAX_AND_OR_EXPRS 100

// NOTE: Enable this for diagnostic purposes
/* #define DEBUG_PRINT_CODE */

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
  Token name;
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
  Scanner *scanner;

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
    mesche_mem_mark_object(ctx->vm, (Object *)ctx->function);

    if (ctx->module != NULL) {
      mesche_mem_mark_object(ctx->vm, (Object *)ctx->module);
    }

    ctx = ctx->parent;
  }
}

static void compiler_init_context(CompilerContext *ctx, CompilerContext *parent,
                                  FunctionType type) {
  if (parent != NULL) {
    ctx->parent = parent;
    ctx->vm = parent->vm;
    ctx->parser = parent->parser;
    ctx->scanner = parent->scanner;
  }

  // Set up the compiler state for this scope
  ctx->function = mesche_object_make_function(ctx->vm, type);
  ctx->function_type = type;
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
  local->name.start = "";
  local->name.length = 0;
}

static void compiler_emit_byte(CompilerContext *ctx, uint8_t byte) {
  mesche_chunk_write(ctx->mem, &ctx->function->chunk, byte, ctx->parser->previous.line);
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
  if (!ctx->parser->had_error) {
    mesche_disasm_function(function);
  }
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

static void compiler_emit_constant(CompilerContext *ctx, Value value) {
  compiler_emit_bytes(ctx, OP_CONSTANT, compiler_make_constant(ctx, value));
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

static void compiler_advance(CompilerContext *ctx) {
  ctx->parser->previous = ctx->parser->current;

  for (;;) {
    ctx->parser->current = mesche_scanner_next_token(ctx->scanner);
    // Consume tokens until we hit a non-error token
    if (ctx->parser->current.kind != TokenKindError)
      break;

    // Create a parse error
    // TODO: Correct error
    compiler_error_at_current(ctx, "Reached a compiler error");
  }
}

static void compiler_consume(CompilerContext *ctx, TokenKind kind, const char *message) {
  if (ctx->parser->current.kind == kind) {
    compiler_advance(ctx);
    return;
  }

  // If we didn't reach the expected token time, return an error
  compiler_error_at_current(ctx, message);
}

static void compiler_consume_sub(CompilerContext *ctx, TokenKind sub_kind, const char *message) {
  if (ctx->parser->current.sub_kind == sub_kind) {
    compiler_advance(ctx);
    return;
  }

  // If we didn't reach the expected token time, return an error
  compiler_error_at_current(ctx, message);
}

// Predefine the main parser function
static void compiler_parse_expr(CompilerContext *ctx);

static void compiler_parse_number(CompilerContext *ctx) {
  double value = strtod(ctx->parser->previous.start, NULL);
  compiler_emit_constant(ctx, NUMBER_VAL(value));
}

static ObjectString *compiler_parse_string_literal(CompilerContext *ctx) {
  return mesche_object_make_string(ctx->vm, ctx->parser->previous.start + 1,
                                   ctx->parser->previous.length - 2);
}

static void compiler_parse_string(CompilerContext *ctx) {
  compiler_emit_constant(ctx, OBJECT_VAL(compiler_parse_string_literal(ctx)));
}

static ObjectKeyword *compiler_parse_keyword_literal(CompilerContext *ctx) {
  return mesche_object_make_keyword(ctx->vm, ctx->parser->previous.start + 1,
                                    ctx->parser->previous.length - 1);
}

static void compiler_parse_keyword(CompilerContext *ctx) {
  compiler_emit_constant(ctx, OBJECT_VAL(compiler_parse_keyword_literal(ctx)));
}

static void compiler_parse_symbol_literal(CompilerContext *ctx) {
  compiler_emit_constant(ctx,
                         OBJECT_VAL(mesche_object_make_symbol(ctx->vm, ctx->parser->previous.start,
                                                              ctx->parser->previous.length)));
}

static void compiler_parse_literal(CompilerContext *ctx) {
  switch (ctx->parser->previous.kind) {
  case TokenKindNil:
    compiler_emit_byte(ctx, OP_NIL);
    break;
  case TokenKindTrue:
    compiler_emit_byte(ctx, OP_T);
    break;
  default:
    return; // We shouldn't hit this
  }
}

static void compiler_parse_block(CompilerContext *ctx, bool expect_end_paren) {
  int previous_tail_count = ctx->tail_site_count;

  for (;;) {
    // Reset the tail sites to log again for the sub-expression
    ctx->tail_site_count = previous_tail_count;

    // Parse the sub-expression
    compiler_parse_expr(ctx);

    // Are we finished parsing expressions?
    if (expect_end_paren && ctx->parser->current.kind == TokenKindRightParen) {
      compiler_consume(ctx, TokenKindRightParen, "Expected closing paren.");

      // Log the possible tail call if there should be one
      compiler_log_tail_site(ctx);

      break;
    } else if (ctx->parser->current.kind == TokenKindEOF) {
      break;
    } else {
      // If we continue the loop, pop the last expression result
      compiler_emit_byte(ctx, OP_POP);
    }
  }
}

static bool compiler_identifiers_equal(Token *a, Token *b) {
  if (a->length != b->length)
    return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

static void compiler_add_local(CompilerContext *ctx, Token name) {
  if (ctx->local_count == UINT8_COUNT) {
    compiler_error(ctx, "Too many local variables defined in function.");
  }

  Local *local = &ctx->locals[ctx->local_count++];
  local->name = name; // No need to copy, will only be used during compilation
  local->depth = -1;  // The variable is uninitialized until assigned
  local->is_captured = false;
}

static int compiler_resolve_local(CompilerContext *ctx, Token *name) {
  // Find the identifier by name in scope chain
  for (int i = ctx->local_count - 1; i >= 0; i--) {
    Local *local = &ctx->locals[i];
    if (compiler_identifiers_equal(name, &local->name)) {
      if (local->depth == -1) {
        compiler_error(ctx, "Referenced variable before it was bound");
      }
      return i;
    }
  }

  // Is the name the same as the function name?
  ObjectString *func_name = ctx->function->name;
  if (func_name && func_name->length == name->length &&
      memcmp(func_name->chars, name->start, func_name->length) == 0) {
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

static int compiler_resolve_upvalue(CompilerContext *ctx, Token *name) {
  // If there's no parent context then there's nothing to close over
  if (ctx->parent == NULL)
    return -1;

  // First try to resolve the variable as a local in the parent
  int local = compiler_resolve_local(ctx->parent, name);
  if (local != -1) {
    ctx->parent->locals[local].is_captured = true;
    return compiler_add_upvalue(ctx, (uint8_t)local, true);
  }

  // If we didn't find a local, look for a binding from a parent scope
  int upvalue = compiler_resolve_upvalue(ctx->parent, name);
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

static void compiler_declare_variable(CompilerContext *ctx) {
  // No need to declare a global, it will be dynamically resolved
  if (ctx->scope_depth == 0)
    return;

  // Start from the most recent local and work backwards to
  // find an existing variable with the same binding in this scope
  Token *name = &ctx->parser->previous;
  for (int i = ctx->local_count - 1; i >= 0; i--) {
    // If the local is in a different scope, stop looking
    Local *local = &ctx->locals[i];
    if (local->depth != -1 && local->depth < ctx->scope_depth)
      break;

    // In the same scope, is the identifier the same?
    if (compiler_identifiers_equal(name, &local->name)) {
      compiler_error(ctx, "Duplicate variable binding in 'let'");
      return;
    }
  }

  // Add a local binding for the name
  compiler_add_local(ctx, *name);
}

static uint8_t compiler_parse_symbol(CompilerContext *ctx, bool is_global) {
  // Declare the variable and exit if we're in a local scope
  if (!is_global && ctx->scope_depth > 0) {
    compiler_declare_variable(ctx);
    return 0;
  }

  Value new_string = OBJECT_VAL(mesche_object_make_string(ctx->vm, ctx->parser->previous.start,
                                                          ctx->parser->previous.length));

  // Reuse an existing constant for the same string if possible
  uint8_t constant = 0;
  bool value_found = false;
  Chunk *chunk = &ctx->function->chunk;
  for (int i = 0; i < chunk->constants.count; i++) {
    if (mesche_value_eqv_p(chunk->constants.values[i], new_string)) {
      constant = i;
      value_found = true;
      break;
    }
  }

  return value_found ? constant : compiler_make_constant(ctx, new_string);
}

static void compiler_parse_identifier(CompilerContext *ctx) {
  // Are we looking at a local variable?
  int local_index = compiler_resolve_local(ctx, &ctx->parser->previous);
  if (local_index != -1) {
    compiler_emit_bytes(ctx, OP_READ_LOCAL, (uint8_t)local_index);
  } else if ((local_index = compiler_resolve_upvalue(ctx, &ctx->parser->previous)) != -1) {
    // Found an upvalue
    compiler_emit_bytes(ctx, OP_READ_UPVALUE, (uint8_t)local_index);
  } else {
    uint8_t variable_constant = compiler_parse_symbol(ctx, true);
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

static void compiler_parse_set(CompilerContext *ctx) {
  compiler_consume(ctx, TokenKindSymbol, "Expected symbol after 'set!'");

  uint8_t instr = OP_SET_LOCAL;
  int arg = compiler_resolve_local(ctx, &ctx->parser->previous);

  // If there isn't a local, use a global variable instead
  if (arg != -1) {
    // Do nothing, all values are already set.
  } else if ((arg = compiler_resolve_upvalue(ctx, &ctx->parser->previous)) != -1) {
    instr = OP_SET_UPVALUE;
  } else {
    arg = compiler_parse_symbol(ctx, true);
    instr = OP_SET_GLOBAL;
  }

  compiler_parse_expr(ctx);
  compiler_emit_bytes(ctx, instr, arg);

  compiler_consume(ctx, TokenKindRightParen, "Expected closing paren.");
}

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

static void compiler_parse_let(CompilerContext *ctx) {
  int previous_tail_count = ctx->tail_site_count;

  // Create a new compiler context for parsing the let body as an inline
  // function
  CompilerContext let_ctx;
  compiler_init_context(&let_ctx, ctx, TYPE_FUNCTION);
  compiler_begin_scope(&let_ctx);

  // Parse the name of the let
  if (let_ctx.parser->current.kind == TokenKindSymbol) {
    if (let_ctx.parser->current.sub_kind != TokenKindNone) {
      compiler_error(&let_ctx, "Used an invalid symbol for named let.");
    }

    // Parse the let name and save it as the function name
    compiler_advance(&let_ctx);
    Token let_name_token = let_ctx.parser->previous;
    ObjectString *let_name =
        mesche_object_make_string(let_ctx.vm, let_name_token.start, let_name_token.length);
    let_ctx.function->name = let_name;
  }

  compiler_consume(&let_ctx, TokenKindLeftParen, "Expected left paren after 'let'");

  // Before parsing arguments, skip ahead to leave enough space to write out the
  // OP_CLOSURE instruction with the right constant value before writing out the
  // OP_CALL instruction.
  int func_offset = ctx->function->chunk.count;
  ctx->function->chunk.count += 2;

  for (;;) {
    if (let_ctx.parser->current.kind == TokenKindRightParen) {
      compiler_consume(&let_ctx, TokenKindRightParen, "Expected right paren to end bindings");
      break;
    }

    compiler_consume(&let_ctx, TokenKindLeftParen, "Expected left paren to start binding pair");

    // Increase the binding (function argument) count
    let_ctx.function->arity++;
    if (let_ctx.function->arity > 255) {
      compiler_error_at_current(&let_ctx, "Let cannot have more than 255 bindings.");
    }

    // Parse the symbol for the binding
    compiler_advance(&let_ctx);
    uint8_t constant = compiler_parse_symbol(&let_ctx, false);
    compiler_define_variable(&let_ctx, constant);

    // Parse the binding value into the *original context* where the let
    // function will be called so that it gets passed as a parameter to the
    // lambda.
    compiler_parse_expr(ctx);

    compiler_consume(&let_ctx, TokenKindRightParen, "Expected right paren to end binding pair");

    // Reset the tail site count so that the binding expression will not be
    // treated as a tail call site
    ctx->tail_site_count = previous_tail_count;
  }

  // Parse the let body
  compiler_parse_block(&let_ctx, true);

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

static void compiler_parse_define_attributes(CompilerContext *ctx,
                                             DefineAttributes *define_attributes) {
  for (;;) {
    if (ctx->parser->current.kind == TokenKindKeyword) {
      compiler_advance(ctx);

      ObjectKeyword *keyword = compiler_parse_keyword_literal(ctx);
      if (memcmp(keyword->string.chars, "export", 7) == 0) {
        define_attributes->is_export = true;
      } else {
        // TODO: Warn on unknown keywords?
      }

      // Don't explicitly free here, the GC will clean it up
    } else {
      break;
    }
  }

  // Look for a docstring
  if (ctx->parser->current.kind == TokenKindString) {
    // Parse the string and store it
    compiler_advance(ctx);
    define_attributes->doc_string = compiler_parse_string_literal(ctx);
  }
}

static bool compiler_compare_keyword(Token keyword, const char *expected_name) {
  return memcmp(keyword.start + 1, expected_name, strlen(expected_name)) == 0;
}

static void compiler_parse_lambda_inner(CompilerContext *ctx, ObjectString *name,
                                        DefineAttributes *define_attributes) {
  // Create a new compiler context for parsing the function body
  CompilerContext func_ctx;
  compiler_init_context(&func_ctx, ctx, TYPE_FUNCTION);
  compiler_begin_scope(&func_ctx);

  ArgType arg_type = ARG_POSITIONAL;
  bool in_keyword_list = false;
  for (;;) {
    // Try to parse each argument until we reach a closing paren
    if (func_ctx.parser->current.kind == TokenKindRightParen) {
      compiler_consume(&func_ctx, TokenKindRightParen,
                       "Expected right paren to end argument list.");
      break;
    }

    if (func_ctx.parser->current.kind == TokenKindKeyword) {
      if (compiler_compare_keyword(func_ctx.parser->current, "keys")) {
        arg_type = ARG_KEYWORD;
      } else if (compiler_compare_keyword(func_ctx.parser->current, "rest")) {
        arg_type = ARG_REST;
      } else {
        compiler_error_at_current(&func_ctx, "Unexpected function definition keyword.");
      }
      compiler_advance(&func_ctx);
    }

    if (arg_type < ARG_KEYWORD) {
      // Increase the function argument count (arity)
      func_ctx.function->arity++;
      if (func_ctx.function->arity > 255) {
        compiler_error_at_current(&func_ctx, "Function cannot have more than 255 parameters.");
      }

      if (arg_type == ARG_REST) {
        if (func_ctx.function->rest_arg_index > 0) {
          compiler_error_at_current(&func_ctx,
                                    "Function cannot have more than one :rest argument.");
        }

        func_ctx.function->rest_arg_index = func_ctx.function->arity;
      }

      // Parse the argument
      // TODO: Ensure a symbol comes next
      compiler_advance(&func_ctx);
      uint8_t constant = compiler_parse_symbol(&func_ctx, false);
      compiler_define_variable(&func_ctx, constant);
    } else {
      compiler_advance(&func_ctx);

      // Check if there is a default value pair
      bool has_default = false;
      if (func_ctx.parser->current.kind == TokenKindLeftParen) {
        compiler_advance(&func_ctx);
        has_default = true;
      }

      // Parse the keyword name and define it as a local variable
      uint8_t constant = compiler_parse_symbol(&func_ctx, false);
      compiler_define_variable(&func_ctx, constant);

      // Parse the default if we're expecting one
      uint8_t default_constant = 0;
      if (has_default) {
        compiler_consume(&func_ctx, TokenKindRightParen,
                         "Expected right paren after keyword default value.");
      }

      // Add the keyword definition to the function
      ObjectString *keyword_name = mesche_object_make_string(ctx->vm, ctx->parser->previous.start,
                                                             ctx->parser->previous.length);
      mesche_vm_stack_push(func_ctx.vm, OBJECT_VAL(keyword_name));
      KeywordArgument keyword_arg = {
          .name = keyword_name,
          .default_index = default_constant,
      };

      mesche_object_function_keyword_add(ctx->mem, func_ctx.function, keyword_arg);
      mesche_vm_stack_pop(func_ctx.vm);
    }
  }

  // Parse any definition attributes if necessary
  if (define_attributes) {
    compiler_parse_define_attributes(&func_ctx, define_attributes);
  }

  // Parse body
  compiler_parse_block(&func_ctx, true);

  // Get the parsed function and store it in a constant
  ObjectFunction *function = compiler_end(&func_ctx);
  function->name = name;
  compiler_emit_bytes(ctx, OP_CLOSURE, compiler_make_constant(ctx, OBJECT_VAL(function)));

  // Write out the references to each upvalue as arguments to OP_CLOSURE
  for (int i = 0; i < function->upvalue_count; i++) {
    compiler_emit_byte(ctx, func_ctx.upvalues[i].is_local ? 1 : 0);
    compiler_emit_byte(ctx, func_ctx.upvalues[i].index);
  }

  // Let the VM know we're back to the parent compiler
  ctx->vm->current_compiler = ctx;
}

static void compiler_parse_lambda(CompilerContext *ctx) {
  // Consume the leading paren and let the shared lambda parser take over
  compiler_consume(ctx, TokenKindLeftParen, "Expected left paren to begin argument list.");
  compiler_parse_lambda_inner(ctx, NULL, NULL);
}

static void compiler_parse_apply(CompilerContext *ctx) {
  // Apply is very simple, it just takes two expressions, one for the function and the other
  // for the list.

  compiler_parse_expr(ctx);
  compiler_parse_expr(ctx);
  compiler_emit_byte(ctx, OP_APPLY);

  compiler_consume(ctx, TokenKindRightParen, "Expected closing paren.");
}

static void compiler_parse_reset(CompilerContext *ctx) {
  // `reset` requires a lambda expression with no arguments
  compiler_consume(ctx, TokenKindLeftParen, "Expected left paren to begin argument list.");
  compiler_consume(ctx, TokenKindSymbol, "Expected lambda expression.");
  if (ctx->parser->previous.sub_kind != TokenKindLambda) {
    compiler_error(ctx, "Expected lambda expression after 'reset'.");
  }

  // Parse the lambda body of the reset expression *after* emitting OP_RESET so
  // that we can set the new reset context before adding the closure to the
  // stack.
  compiler_emit_byte(ctx, OP_RESET);
  compiler_parse_lambda(ctx);
  compiler_emit_call(ctx, 0, 0);
  compiler_emit_byte(ctx, OP_NOP); // Avoid turning this into a tail call!
  compiler_consume(ctx, TokenKindRightParen, "Expected closing paren.");
}

static void compiler_parse_shift(CompilerContext *ctx) {
  // `shift` requires a lambda expression with one arguments
  compiler_consume(ctx, TokenKindLeftParen, "Expected left paren to begin argument list.");
  compiler_consume(ctx, TokenKindSymbol, "Expected lambda expression.");
  if (ctx->parser->previous.sub_kind != TokenKindLambda) {
    compiler_error(ctx, "Expected lambda expression after 'shift'.");
  }

  // Parse the lambda body of the reset expression *before* emitting OP_SHIFT so
  // that the shift body will be invoked with the continuation function as its
  // parameter.
  compiler_parse_lambda(ctx);
  compiler_emit_byte(ctx, OP_SHIFT);
  compiler_emit_call(ctx, 1, 0);
  compiler_emit_byte(ctx, OP_NOP); // Avoid turning this into a tail call!
  compiler_consume(ctx, TokenKindRightParen, "Expected closing paren.");
}

static void compiler_parse_define(CompilerContext *ctx) {
  DefineAttributes define_attributes;
  define_attributes.is_export = false;
  define_attributes.doc_string = NULL;

  // The next symbol should either be a symbol or an open paren to define a
  // function
  bool is_func = false;
  if (ctx->parser->current.kind == TokenKindLeftParen) {
    compiler_consume(ctx, TokenKindLeftParen, "Expected left paren after 'define'");
    is_func = true;
  }

  compiler_consume(ctx, TokenKindSymbol, "Expected symbol after 'define'");

  uint8_t variable_constant = compiler_parse_symbol(ctx, true);
  if (is_func) {
    // Let the lambda parser take over
    compiler_parse_lambda_inner(ctx,
                                AS_STRING(ctx->function->chunk.constants.values[variable_constant]),
                                &define_attributes);
  } else {
    // Parse a normal expression
    compiler_parse_expr(ctx);
    compiler_parse_define_attributes(ctx, &define_attributes);
    compiler_consume(ctx, TokenKindRightParen, "Expected closing paren.");
  }

  // TODO: Only allow defines at the top of let/lambda bodies
  compiler_define_variable_ex(ctx, variable_constant, &define_attributes);
}

static void compiler_parse_module_name(CompilerContext *ctx) {
  char *module_name = NULL;
  uint8_t symbol_count = 0;

  for (;;) {
    // Bail out when we hit the closing parentheses
    if (ctx->parser->current.kind == TokenKindRightParen) {
      compiler_advance(ctx);
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

      return;
    }

    compiler_consume(ctx, TokenKindSymbol, "Module names can only be comprised of symbols.");

    // Create the module name string from all subsequent symbols
    if (symbol_count == 0) {
      module_name = malloc(sizeof(char) * (ctx->parser->previous.length + 1));
      memcpy(module_name, ctx->parser->previous.start, sizeof(char) * ctx->parser->previous.length);
      module_name[ctx->parser->previous.length] = '\0';
    } else {
      char *prev_name = module_name;
      module_name =
          mesche_cstring_join(module_name, strlen(module_name), ctx->parser->previous.start,
                              ctx->parser->previous.length, " ");
      free(prev_name);
    }

    symbol_count++;
  }
}

static void compiler_parse_define_module(CompilerContext *ctx) {
  compiler_consume(ctx, TokenKindLeftParen, "Expected left paren after 'define-module'");

  // Read the symbol list for the module path
  compiler_parse_module_name(ctx);
  compiler_emit_byte(ctx, OP_DEFINE_MODULE);

  // Store the module name if we're currently parsing a module file
  if (ctx->module) {
    ctx->module->name =
        AS_STRING(ctx->function->chunk.constants.values[ctx->function->chunk.constants.count - 1]);
  }

  // Check for a possible 'import' expression
  if (ctx->parser->current.kind == TokenKindLeftParen) {
    compiler_consume(ctx, TokenKindLeftParen, "Expected left paren to start inner expression.");
    compiler_consume_sub(ctx, TokenKindImport, "Expected 'import' inside of 'define-module'.");

    // There can be multiple import specifications
    for (;;) {
      if (ctx->parser->current.kind == TokenKindRightParen) {
        // Import list is done
        compiler_advance(ctx);
        break;
      }

      compiler_consume(ctx, TokenKindLeftParen, "Expected left paren after 'import'");

      compiler_parse_module_name(ctx);
      compiler_emit_byte(ctx, OP_RESOLVE_MODULE);
      compiler_emit_byte(ctx, OP_IMPORT_MODULE);
      compiler_emit_byte(ctx, OP_POP);
    }
  }

  compiler_consume(ctx, TokenKindRightParen,
                   "Expected right paren to complete 'define-module' expression.");
}

static void compiler_parse_define_record_type(CompilerContext *ctx) {
  compiler_consume(ctx, TokenKindSymbol, "Expected symbol for record name.");
  compiler_parse_symbol_literal(ctx);

  compiler_consume(ctx, TokenKindLeftParen, "Expected left paren after 'define-record-type'");
  compiler_consume(ctx, TokenKindSymbol, "Expected 'fields' after 'define-record-type'.");

  uint8_t field_count = 0;
  if (memcmp(ctx->parser->previous.start, "fields", 6) == 0) {
    // Read the key-value pairs inside of the form
    for (;;) {
      // Exit if we've reached the end of fields
      if (ctx->parser->current.kind == TokenKindRightParen) {
        compiler_advance(ctx);
        break;
      }

      if (ctx->parser->current.kind == TokenKindSymbol) {
        compiler_advance(ctx);
        compiler_parse_symbol_literal(ctx);
        compiler_emit_constant(ctx, NIL_VAL);
        field_count++;
      }
    }
  } else {
    compiler_error(ctx, "Expected 'fields' after 'define-record-type'.");
  }

  compiler_consume(ctx, TokenKindRightParen, "Expected right paren to end 'define-record-type'.");

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

static void compiler_parse_if(CompilerContext *ctx) {
  int previous_tail_count = ctx->tail_site_count;

  // Parse predicate and write out a jump instruction to the else code
  // path if the predicate evaluates to false
  compiler_parse_expr(ctx);
  int jump_origin = compiler_emit_jump(ctx, OP_JUMP_IF_FALSE);

  // Restore the tail count because the predicate expression should never
  // produce a tail call
  ctx->tail_site_count = previous_tail_count;

  // Include a pop so that the expression value gets removed from the stack in
  // the truth path
  compiler_emit_byte(ctx, OP_POP);

  // Parse truth expr and log the tail call if the expression was still in a
  // tail context afterward
  compiler_parse_expr(ctx);
  compiler_log_tail_site(ctx);

  // Write out a jump from the end of the truth case to the end of the expression
  int else_jump = compiler_emit_jump(ctx, OP_JUMP);

  // Patch the jump instruction which leads to the else path
  compiler_patch_jump(ctx, jump_origin);

  // Include a pop so that the predicate expression value gets removed from the stack
  compiler_emit_byte(ctx, OP_POP);

  // Is there an else expression?
  if (ctx->parser->current.kind != TokenKindRightParen) {
    // Parse false expr
    compiler_parse_expr(ctx);
    compiler_log_tail_site(ctx);
  } else {
    // Push a 'nil' onto the value stack if there was no else
    compiler_emit_byte(ctx, OP_NIL);
  }

  // Patch the jump instruction after the false path has been compiled
  compiler_patch_jump(ctx, else_jump);

  compiler_consume(ctx, TokenKindRightParen, "Expected right paren to end 'if' expression");
}

static void compiler_parse_or(CompilerContext *ctx) {
  int prev_jump = -1;
  int expr_count = 0;
  int end_jumps[MAX_AND_OR_EXPRS];

  for (;;) {
    // Exit if we've reached the end of the expressions
    if (ctx->parser->current.kind == TokenKindRightParen) {
      compiler_advance(ctx);
      break;
    }

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
    compiler_parse_expr(ctx);
    expr_count++;

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

static void compiler_parse_and(CompilerContext *ctx) {
  int prev_jump = -1;
  int expr_count = 0;
  int end_jumps[MAX_AND_OR_EXPRS];

  for (;;) {
    // Exit if we've reached the end of the expressions
    if (ctx->parser->current.kind == TokenKindRightParen) {
      compiler_advance(ctx);
      break;
    }

    if (expr_count > 0) {
      // If it evaluates to false, jump to the end of the expression list
      end_jumps[expr_count - 1] = compiler_emit_jump(ctx, OP_JUMP_IF_FALSE);

      // Emit a pop to remove the previous expression value from the stack
      compiler_emit_byte(ctx, OP_POP);
    }

    // Parse the expression
    compiler_parse_expr(ctx);
    expr_count++;

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

static void compiler_parse_operator_call(CompilerContext *ctx, Token *call_token,
                                         uint8_t operand_count) {
  TokenKind operator= call_token->sub_kind;
  switch (operator) {
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
    return; // We shouldn't hit this
  }
}

static void compiler_parse_module_import(CompilerContext *ctx) {
  compiler_consume(ctx, TokenKindLeftParen, "Expected left paren after 'module-import'");
  compiler_parse_module_name(ctx);
  compiler_emit_byte(ctx, OP_RESOLVE_MODULE);
  compiler_emit_byte(ctx, OP_IMPORT_MODULE);
  compiler_consume(ctx, TokenKindRightParen, "Expected right paren to complete 'module-import'");
}

static void compiler_parse_module_enter(CompilerContext *ctx) {
  compiler_consume(ctx, TokenKindLeftParen, "Expected left paren after 'module-enter'");
  compiler_parse_module_name(ctx);
  compiler_emit_byte(ctx, OP_ENTER_MODULE);
  compiler_consume(ctx, TokenKindRightParen, "Expected right paren to complete 'module-enter'");
}

static void compiler_parse_load_file(CompilerContext *ctx) {
  compiler_parse_expr(ctx);
  compiler_emit_byte(ctx, OP_LOAD_FILE);
  compiler_consume(ctx, TokenKindRightParen, "Expected right paren to complete 'load-file'");
}

static bool compiler_parse_special_form(CompilerContext *ctx, Token *call_token) {
  TokenKind operator= call_token->sub_kind;
  switch (operator) {
  case TokenKindOr:
    compiler_parse_or(ctx);
    break;
  case TokenKindAnd:
    compiler_parse_and(ctx);
    break;
  case TokenKindBegin:
    compiler_parse_block(ctx, true);
    break;
  case TokenKindDefine:
    compiler_parse_define(ctx);
    break;
  case TokenKindDefineModule:
    compiler_parse_define_module(ctx);
    break;
  case TokenKindModuleImport:
    compiler_parse_module_import(ctx);
    break;
  case TokenKindModuleEnter:
    compiler_parse_module_enter(ctx);
    break;
  case TokenKindDefineRecordType:
    compiler_parse_define_record_type(ctx);
    break;
  case TokenKindLoadFile:
    compiler_parse_load_file(ctx);
    break;
  case TokenKindSet:
    compiler_parse_set(ctx);
    break;
  case TokenKindLet:
    compiler_parse_let(ctx);
    break;
  case TokenKindIf:
    compiler_parse_if(ctx);
    break;
  case TokenKindLambda:
    compiler_parse_lambda(ctx);
    break;
  case TokenKindApply:
    compiler_parse_apply(ctx);
    break;
  case TokenKindReset:
    compiler_parse_reset(ctx);
    break;
  case TokenKindShift:
    compiler_parse_shift(ctx);
    break;
  case TokenKindBreak:
    // Just emit OP_BREAK and be done
    compiler_emit_byte(ctx, OP_BREAK);
    compiler_consume(ctx, TokenKindRightParen, "Expected closing paren.");
    break;
  default:
    return false; // No special form found
  }

  return true;
}

static void compiler_parse_quoted_list(CompilerContext *ctx) {
  bool is_backquote = ctx->parser->previous.kind == TokenKindBackquote;

  // If this is a quoted symbol, just parse it and return
  if (ctx->parser->current.kind == TokenKindSymbol) {
    compiler_advance(ctx);
    compiler_parse_symbol_literal(ctx);
    return;
  }

  // Ensure the open paren is there
  compiler_consume(ctx, TokenKindLeftParen, "Expected left paren after quote.");

  uint8_t item_count = 0;
  for (;;) {
    // Bail out when we hit the closing parentheses
    if (ctx->parser->current.kind == TokenKindRightParen) {
      // Emit the list operation
      compiler_advance(ctx);
      compiler_emit_bytes(ctx, OP_LIST, item_count);
      return;
    }

    // Parse certain token types differently in quoted lists
    switch (ctx->parser->current.kind) {
    case TokenKindSymbol:
      compiler_advance(ctx);
      compiler_parse_symbol_literal(ctx);
      break;
    case TokenKindLeftParen:
      compiler_parse_quoted_list(ctx);
      break;
    case TokenKindUnquote:
      // Evaluate whatever the next expression is
      compiler_advance(ctx);

      if (ctx->parser->current.kind == TokenKindSplice) {
        PANIC("Splicing is not currently supported.\n");
      }

      if (!is_backquote) {
        compiler_error(ctx, "Cannot use unquote in a non-backquoted expression.");
        compiler_parse_quoted_list(ctx);
      } else {
        compiler_parse_expr(ctx);
      }
      break;
    case TokenKindPlus:
    case TokenKindMinus:
    case TokenKindSlash:
    case TokenKindStar:
      compiler_advance(ctx);
      compiler_parse_symbol_literal(ctx);
      break;
    default:
      compiler_parse_expr(ctx);
    }

    item_count++;
  }
}

static void compiler_parse_list(CompilerContext *ctx) {
  // Try to find the call target (this could be an expression!)
  Token call_token = ctx->parser->current;
  bool is_call = false;

  // Possibilities
  // - Primitive command with its own opcode
  // - Special form that has non-standard call semantics
  // - Symbol in first position
  // - Raw lambda in first position
  // - Expression that evaluates to lambda
  // In the latter 3 cases, compiler the callee before the arguments

  // Evaluate the first expression if it's not an operator
  if ((call_token.kind == TokenKindSymbol && call_token.sub_kind == TokenKindNone) ||
      call_token.kind == TokenKindLeftParen) {
    compiler_parse_expr(ctx);
    is_call = true;
  } else {
    compiler_advance(ctx);
    if (compiler_parse_special_form(ctx, &call_token)) {
      // A special form was parsed, exit here
      return;
    }
  }

  // Parse argument expressions until we reach a right paren
  uint8_t arg_count = 0;
  uint8_t keyword_count = 0;
  bool in_keyword_args = false;
  for (;;) {
    // Bail out when we hit the closing parentheses
    if (ctx->parser->current.kind == TokenKindRightParen) {
      if (is_call == false) {
        // Compile the primitive operator
        compiler_parse_operator_call(ctx, &call_token, arg_count);
      } else {
        // If this is a legitimate call, it can be turned into a tail call
        compiler_emit_call(ctx, arg_count, keyword_count);
      }

      // Consume the right paren and exit
      compiler_consume(ctx, TokenKindRightParen, "Expected closing paren.");
      return;
    }

    // Is it a keyword argument?
    if (!in_keyword_args && ctx->parser->current.kind == TokenKindKeyword) {
      // We're only looking for keyword arguments from this point on
      in_keyword_args = true;
    }

    if (in_keyword_args) {
      // Parse the keyword and value
      compiler_consume(ctx, TokenKindKeyword, "Expected keyword.");
      compiler_parse_keyword(ctx);
      compiler_parse_expr(ctx);
      keyword_count++; // Add one more argument for the value we just parsed
    } else {
      // Compile next positional parameter
      compiler_parse_expr(ctx);
      arg_count++;
    }

    if (arg_count == 255) {
      compiler_error(ctx, "Cannot pass more than 255 arguments in a function call.");
    }
    if (arg_count == 15) {
      compiler_error(ctx, "Cannot pass more than 15 keyword arguments in a function call.");
    }
  }
}

void (*ParserFunc)(CompilerContext *ctx);

static void compiler_parse_expr(CompilerContext *ctx) {
  if (ctx->parser->current.kind == TokenKindEOF) {
    return;
  }

  // An expression can either be a single element or a list
  if (ctx->parser->current.kind == TokenKindLeftParen) {
    compiler_advance(ctx);
    compiler_parse_list(ctx);
    return;
  }

  // Is it a list that starts with a quote?
  if (ctx->parser->current.kind == TokenKindQuote ||
      ctx->parser->current.kind == TokenKindBackquote) {
    compiler_advance(ctx);
    compiler_parse_quoted_list(ctx);
    return;
  }

  // Must be an atom
  if (ctx->parser->current.kind == TokenKindNumber) {
    compiler_advance(ctx);
    compiler_parse_number(ctx);
  } else if (ctx->parser->current.kind == TokenKindNil) {
    compiler_advance(ctx);
    compiler_parse_literal(ctx);
  } else if (ctx->parser->current.kind == TokenKindTrue) {
    compiler_advance(ctx);
    compiler_parse_literal(ctx);
  } else if (ctx->parser->current.kind == TokenKindString) {
    compiler_advance(ctx);
    compiler_parse_string(ctx);
  } else if (ctx->parser->current.kind == TokenKindKeyword) {
    compiler_advance(ctx);
    compiler_parse_keyword(ctx);
  } else if (ctx->parser->current.kind == TokenKindSymbol) {
    compiler_advance(ctx);
    compiler_parse_identifier(ctx);
  } else {
    compiler_error_at_current(ctx, "Premature end of expression.");
  }
}

ObjectFunction *mesche_compile_source(VM *vm, const char *script_source) {
  Parser parser;
  Scanner scanner;
  mesche_scanner_init(&scanner, script_source);

  // Set up the context
  CompilerContext ctx = {
      .vm = vm,
      .parser = &parser,
      .scanner = &scanner,
  };
  compiler_init_context(&ctx, NULL, TYPE_SCRIPT);

  // Reset parser error state
  parser.had_error = false;
  parser.panic_mode = false;

  // Find the first legitimate token and then start parsing until
  // we reach the end of the source
  compiler_advance(&ctx);
  compiler_parse_block(&ctx, false);
  compiler_consume(&ctx, TokenKindEOF, "Expected end of expression.");

  // Retrieve the final function
  ObjectFunction *function = compiler_end(&ctx);

  // Clear the VM's pointer to this compiler
  vm->current_compiler = NULL;

  // Return the compiled function if there were no parse errors
  return parser.had_error ? NULL : function;
}

ObjectModule *mesche_compile_module(VM *vm, ObjectModule *module, const char *module_source) {
  // TODO: Deduplicate implementation code!
  Parser parser;
  Scanner scanner;
  mesche_scanner_init(&scanner, module_source);

  // Set up the context
  CompilerContext ctx = {.vm = vm, .parser = &parser, .scanner = &scanner, .module = module};
  compiler_init_context(&ctx, NULL, TYPE_SCRIPT);

  // Reset parser error state
  parser.had_error = false;
  parser.panic_mode = false;

  // Find the first legitimate token and then start parsing until
  // we reach the end of the source
  compiler_advance(&ctx);
  compiler_parse_block(&ctx, false);
  compiler_consume(&ctx, TokenKindEOF, "Expected end of expression.");

  // Retrieve the final function and assign it to the module
  ObjectFunction *function = compiler_end(&ctx);

  // Ensure the user defined a module in the file
  // TODO: How can we tell if the name is already set?
  if (ctx.module->name == NULL) {
    compiler_error(&ctx, "A valid module definition was not found in the source file.");
  }

  // Assign the function as the module's top-level body
  ctx.module->init_function = function;

  // Clear the VM's pointer to this compiler
  vm->current_compiler = NULL;

  return parser.had_error ? NULL : ctx.module;
}
