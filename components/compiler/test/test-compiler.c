#include "../src/compiler.h"
#include "../src/error.h"
#include "../src/object.h"
#include "../src/op.h"
#include "../src/reader.h"
#include "../src/scanner.h"
#include "../src/vm-impl.h"
#include "test.h"

static VM vm;
static Value read_value;
static Value compile_result;
static Reader reader;
static ObjectFunction *out_func;
static int byte_idx = 0;
static uint16_t jump_val;

#define COMPILER_INIT()                                                                            \
  InterpretResult result;                                                                          \
  mesche_vm_init(&vm, 0, NULL);

#define USE_CORE() mesche_vm_register_core_modules(&vm, "./modules");

#define COMPILE(source)                                                                            \
  byte_idx = 0;                                                                                    \
  mesche_reader_from_string(&vm.reader_context, &reader, source);                                  \
  compile_result = mesche_compile_source(&vm, &reader);                                            \
  if (IS_ERROR(compile_result)) {                                                                  \
    out_func = NULL;                                                                               \
  } else {                                                                                         \
    out_func = AS_FUNCTION(compile_result);                                                        \
  }

#define ASSERT_NO_ERROR(expected_message)                                                          \
  if (IS_ERROR(compile_result)) {                                                                  \
    FAIL("Received an unexpected error: %s", AS_ERROR(compile_result)->message->chars);            \
  }

#define ASSERT_ERROR(expected_message)                                                             \
  if (!IS_ERROR(compile_result)) {                                                                 \
    FAIL("Expected error but received result!");                                                   \
  } else if (strncmp(AS_ERROR(compile_result)->message->chars, expected_message,                   \
                     strlen(expected_message)) != 0) {                                             \
    FAIL("Received an unexpected error: %s", AS_ERROR(compile_result)->message->chars);            \
  }

#define CHECK_SET_FUNC(func)                                                                       \
  out_func = func;                                                                                 \
  byte_idx = 0;

#define CHECK_BYTE(byte)                                                                           \
  if (out_func->chunk.code[byte_idx] != byte) {                                                    \
    FAIL("Expected byte %s, got %d\n           at line %d", __stringify(byte),                     \
         out_func->chunk.code[byte_idx], __LINE__);                                                \
  }                                                                                                \
  byte_idx++;

#define CHECK_BYTES(byte1, byte2)                                                                  \
  CHECK_BYTE(byte1);                                                                               \
  CHECK_BYTE(byte2);

#define CHECK_JUMP(instr, source, dest)                                                            \
  CHECK_BYTE(instr);                                                                               \
  byte_idx += 2;

#define CHECK_CALL(instr, arg_count, keyword_count)                                                \
  CHECK_BYTE(instr);                                                                               \
  CHECK_BYTE(arg_count);                                                                           \
  CHECK_BYTE(keyword_count);

#define CHECK_STRING(expected_str, const_index)                                                    \
  if (memcmp(AS_CSTRING(out_func->chunk.constants.values[const_index]), expected_str,              \
             strlen(expected_str)) != 0) {                                                         \
    FAIL("Expected string %s, got %s", expected_str,                                               \
         AS_CSTRING(out_func->chunk.constants.values[const_index]));                               \
  }

/* jump_val = (uint16_t)(out_func->chunk.code[byte_idx + 1] << 8); \ */
/* jump_val |= out_func->chunk.code[byte_idx + 2]; \ */
/* ASSERT_INT(byte_idx, offset + 3 + sign * jump_val); */

static void compiles_quoted_exprs() {
  COMPILER_INIT();

  COMPILE("'(1 2 3) "
          "'() "
          "(quote '()) "
          "'35");

  CHECK_BYTES(OP_CONSTANT, 0);
  if (!IS_CONS(out_func->chunk.constants.values[0])) {
    FAIL("Not a cons!");
  }

  CHECK_BYTE(OP_POP);
  CHECK_BYTES(OP_CONSTANT, 1);
  if (!IS_EMPTY(out_func->chunk.constants.values[1])) {
    FAIL("Not an empty list!");
  }

  CHECK_BYTE(OP_POP);
  CHECK_BYTES(OP_CONSTANT, 2);
  Value constant = out_func->chunk.constants.values[2];
  if (!IS_CONS(constant) || !IS_SYMBOL(AS_CONS(constant)->car) ||
      !IS_EMPTY(AS_CONS(AS_CONS(constant)->cdr)->car)) {
    FAIL("Not a quoted empty list!");
  }

  CHECK_BYTE(OP_POP);
  CHECK_BYTES(OP_CONSTANT, 3);
  if (!IS_NUMBER(out_func->chunk.constants.values[3])) {
    FAIL("Not a number!");
  }

  CHECK_BYTE(OP_RETURN);

  PASS();
}

static void compiles_module_import() {
  COMPILER_INIT();

  COMPILE("(module-import (test alpha))");

  CHECK_BYTES(OP_CONSTANT, 0);
  CHECK_BYTE(OP_IMPORT_MODULE);
  CHECK_BYTE(OP_RETURN);

  PASS();
}

static void compiles_lambda() {
  COMPILER_INIT();

  COMPILE("((lambda (x y)"
          "  (+ x y))"
          " 3 4)");

  ASSERT_NO_ERROR();
  CHECK_BYTES(OP_CLOSURE, 0);
  CHECK_BYTES(OP_CONSTANT, 1);
  CHECK_BYTES(OP_CONSTANT, 2);
  CHECK_CALL(OP_CALL, 2, 0);
  CHECK_BYTE(OP_RETURN);

  CHECK_SET_FUNC(AS_FUNCTION(out_func->chunk.constants.values[0]));
  /* CHECK_BYTES(OP_READ_GLOBAL, 0); */
  /* CHECK_BYTES(OP_READ_LOCAL, 1); */
  /* CHECK_BYTES(OP_READ_LOCAL, 2); */
  /* CHECK_CALL(OP_TAIL_CALL, 2, 0); */
  CHECK_BYTES(OP_READ_LOCAL, 1);
  CHECK_BYTES(OP_READ_LOCAL, 2);
  CHECK_BYTE(OP_ADD);
  CHECK_BYTE(OP_RETURN);

  PASS();
}

static void compiles_let() {
  COMPILER_INIT();

  COMPILE("(let ((x 3)\n"
          "      (y 4))\n"
          "  (+ x y))");

  // A let is turned into an anonymous lambda, so check both the usage site and
  // the lambda definition

  CHECK_BYTES(OP_CLOSURE, 2);
  CHECK_BYTES(OP_CONSTANT, 0);
  CHECK_BYTES(OP_CONSTANT, 1);
  CHECK_CALL(OP_CALL, 2, 0);
  CHECK_BYTE(OP_RETURN);

  CHECK_SET_FUNC(AS_FUNCTION(out_func->chunk.constants.values[2]));
  /* CHECK_BYTES(OP_READ_GLOBAL, 0); */
  /* CHECK_BYTES(OP_READ_LOCAL, 1); */
  /* CHECK_BYTES(OP_READ_LOCAL, 2); */
  /* CHECK_CALL(OP_TAIL_CALL, 2, 0); */
  CHECK_BYTES(OP_READ_LOCAL, 1);
  CHECK_BYTES(OP_READ_LOCAL, 2);
  CHECK_BYTE(OP_ADD);
  CHECK_BYTE(OP_RETURN);

  PASS();
}

static void compiles_named_let() {
  COMPILER_INIT();

  COMPILE("(let test-let ((x 3) (y 4))"
          "  (test-let (* x y) y))");

  // A let is turned into an anonymous lambda, so check both the usage site and
  // the lambda definition

  CHECK_BYTES(OP_CLOSURE, 2);
  CHECK_BYTES(OP_CONSTANT, 0);
  CHECK_BYTES(OP_CONSTANT, 1);
  CHECK_CALL(OP_CALL, 2, 0); // In script scope, not a tail call!
  CHECK_BYTE(OP_RETURN);

  CHECK_SET_FUNC(AS_FUNCTION(out_func->chunk.constants.values[2]));

  CHECK_BYTES(OP_READ_LOCAL, 0);
  /* CHECK_BYTES(OP_READ_GLOBAL, 0); */
  /* CHECK_BYTES(OP_READ_LOCAL, 1); */
  /* CHECK_BYTES(OP_READ_LOCAL, 2); */
  /* CHECK_CALL(OP_CALL, 2, 0); */
  CHECK_BYTES(OP_READ_LOCAL, 1);
  CHECK_BYTES(OP_READ_LOCAL, 2);
  CHECK_BYTE(OP_MULTIPLY);
  CHECK_BYTES(OP_READ_LOCAL, 2);
  CHECK_CALL(OP_TAIL_CALL, 2, 0);
  CHECK_BYTE(OP_RETURN);

  PASS();
}

static void compiles_if_expr() {
  COMPILER_INIT();

  COMPILE("(if #t \n"
          "    (+ 3 1)\n"
          "    2)");

  CHECK_BYTE(OP_TRUE);
  CHECK_JUMP(OP_JUMP_IF_FALSE, 1, 17);
  CHECK_BYTE(OP_POP);
  /* CHECK_BYTES(OP_READ_GLOBAL, 0); */
  /* CHECK_BYTES(OP_CONSTANT, 1); */
  /* CHECK_BYTES(OP_CONSTANT, 2); */
  /* CHECK_CALL(OP_CALL, 2, 0); */
  CHECK_BYTES(OP_CONSTANT, 0);
  CHECK_BYTES(OP_CONSTANT, 1);
  CHECK_BYTE(OP_ADD);
  CHECK_JUMP(OP_JUMP, 14, 20);
  CHECK_BYTE(OP_POP);
  /* CHECK_BYTES(OP_CONSTANT, 3); */
  CHECK_BYTES(OP_CONSTANT, 2);
  CHECK_BYTE(OP_RETURN);

  PASS();
}

static void compiles_or_expr() {
  COMPILER_INIT();

  COMPILE("(or #f 2 3)");

  CHECK_BYTE(OP_FALSE);
  CHECK_JUMP(OP_JUMP_IF_FALSE, 1, 7);
  CHECK_JUMP(OP_JUMP, 4, 19);

  CHECK_BYTE(OP_POP);
  CHECK_BYTES(OP_CONSTANT, 0);
  CHECK_JUMP(OP_JUMP_IF_FALSE, 10, 16);
  CHECK_JUMP(OP_JUMP, 13, 19);

  CHECK_BYTE(OP_POP);
  CHECK_BYTES(OP_CONSTANT, 1);
  CHECK_BYTE(OP_RETURN);

  PASS();
}

static void compiles_and_expr() {
  COMPILER_INIT();

  COMPILE("(and 2 #f 3)");

  CHECK_BYTES(OP_CONSTANT, 0);
  CHECK_JUMP(OP_JUMP_IF_FALSE, 2, 21);

  CHECK_BYTE(OP_POP);
  CHECK_BYTE(OP_FALSE);
  CHECK_JUMP(OP_JUMP_IF_FALSE, 7, 16);

  CHECK_BYTE(OP_POP);
  CHECK_BYTES(OP_CONSTANT, 1);
  CHECK_BYTE(OP_RETURN);

  PASS();
}

static void compiles_if_expr_no_else() {
  COMPILER_INIT();

  COMPILE("(if #t (+ 3 1))");

  CHECK_BYTE(OP_TRUE);
  CHECK_JUMP(OP_JUMP_IF_FALSE, 1, 17);
  CHECK_BYTE(OP_POP);
  /* CHECK_BYTES(OP_READ_GLOBAL, 0); */
  /* CHECK_BYTES(OP_CONSTANT, 1); */
  /* CHECK_BYTES(OP_CONSTANT, 2); */
  /* CHECK_CALL(OP_CALL, 2, 0); */
  CHECK_BYTES(OP_CONSTANT, 0);
  CHECK_BYTES(OP_CONSTANT, 1);
  CHECK_BYTE(OP_ADD);
  CHECK_JUMP(OP_JUMP, 14, 19);
  CHECK_BYTE(OP_POP);
  CHECK_BYTE(OP_FALSE);
  CHECK_BYTE(OP_RETURN);

  PASS();
}

static void compiles_lambda_rest_args() {
  COMPILER_INIT();

  COMPILE("(define (rest-func x y . args)"
          "  (display args))");

  ObjectFunction *func = AS_FUNCTION(out_func->chunk.constants.values[1]);
  ASSERT_INT(3, func->rest_arg_index);

  COMPILE("(define x (lambda (x y . args)"
          "            (display args)))");

  func = AS_FUNCTION(out_func->chunk.constants.values[1]);
  ASSERT_INT(3, func->rest_arg_index);

  COMPILE("(define (rest-func . args)"
          "  (display args))");

  func = AS_FUNCTION(out_func->chunk.constants.values[1]);
  ASSERT_INT(1, func->rest_arg_index);

  COMPILE("(define x (lambda args"
          "            (display args)))");

  func = AS_FUNCTION(out_func->chunk.constants.values[1]);
  ASSERT_INT(1, func->rest_arg_index);

  PASS();
}

static void compiles_define_simple_binding() {
  COMPILER_INIT();

  COMPILE("(define syntaxes"
          "  (list (cons 'and 1)"
          "        (cons 'or 2)))");

  /* CHECK_BYTES(OP_READ_GLOBAL, 1); */
  /* CHECK_BYTES(OP_READ_GLOBAL, 2); */
  /* CHECK_BYTES(OP_CONSTANT, 3); */
  /* CHECK_BYTES(OP_CONSTANT, 4); */
  /* CHECK_CALL(OP_CALL, 2, 0); */
  /* CHECK_BYTES(OP_READ_GLOBAL, 2); */
  /* CHECK_BYTES(OP_CONSTANT, 5); */
  /* CHECK_BYTES(OP_CONSTANT, 6); */
  /* CHECK_CALL(OP_CALL, 2, 0); */
  /* CHECK_CALL(OP_CALL, 2, 0); */

  CHECK_BYTES(OP_CONSTANT, 1);
  CHECK_BYTES(OP_CONSTANT, 2);
  CHECK_BYTE(OP_CONS);
  CHECK_BYTES(OP_CONSTANT, 3);
  CHECK_BYTES(OP_CONSTANT, 4);
  CHECK_BYTE(OP_CONS);
  CHECK_BYTES(OP_LIST, 2);
  CHECK_BYTES(OP_DEFINE_GLOBAL, 0);
  CHECK_BYTE(OP_RETURN);

  PASS();
}

static void compiles_define_record_type() {
  COMPILER_INIT();

  COMPILE("(define-record-type channel"
          "  (fields senders receivers))");

  CHECK_BYTES(OP_CONSTANT, 0);
  CHECK_BYTES(OP_CONSTANT, 1);
  CHECK_BYTES(OP_CONSTANT, 2);
  CHECK_BYTES(OP_CONSTANT, 3);
  CHECK_BYTES(OP_CONSTANT, 4);
  CHECK_BYTES(OP_DEFINE_RECORD, 2);
  CHECK_BYTE(OP_RETURN);

  PASS();
}

static void compiles_define_attributes() {
  COMPILER_INIT();

  COMPILE("(define (test-func x) :export"
          "  (+ x 1))");

  CHECK_SET_FUNC(AS_FUNCTION(out_func->chunk.constants.values[1]));
  /* CHECK_BYTES(OP_READ_GLOBAL, 0); */
  /* CHECK_BYTES(OP_READ_LOCAL, 1); */
  /* CHECK_BYTES(OP_CONSTANT, 1); */
  /* CHECK_CALL(OP_TAIL_CALL, 2, 0); */
  CHECK_BYTES(OP_READ_LOCAL, 1);
  CHECK_BYTES(OP_CONSTANT, 0);
  CHECK_BYTE(OP_ADD);
  CHECK_BYTE(OP_RETURN);

  COMPILE("(define (test-func x)"
          "\"This is a documentation string!\""
          "  (+ x 1))");

  CHECK_SET_FUNC(AS_FUNCTION(out_func->chunk.constants.values[1]));
  /* CHECK_BYTES(OP_READ_GLOBAL, 0); */
  /* CHECK_BYTES(OP_READ_LOCAL, 1); */
  /* CHECK_BYTES(OP_CONSTANT, 1); */
  /* CHECK_CALL(OP_TAIL_CALL, 2, 0); */
  CHECK_BYTES(OP_READ_LOCAL, 1);
  CHECK_BYTES(OP_CONSTANT, 0);
  CHECK_BYTE(OP_ADD);
  CHECK_BYTE(OP_RETURN);

  COMPILE("(define (test-func x) :export"
          "\"This is a documentation string!\""
          "  (+ x 1))");

  CHECK_SET_FUNC(AS_FUNCTION(out_func->chunk.constants.values[1]));
  /* CHECK_BYTES(OP_READ_GLOBAL, 0); */
  /* CHECK_BYTES(OP_READ_LOCAL, 1); */
  /* CHECK_BYTES(OP_CONSTANT, 1); */
  /* CHECK_CALL(OP_TAIL_CALL, 2, 0); */
  CHECK_BYTES(OP_READ_LOCAL, 1);
  CHECK_BYTES(OP_CONSTANT, 0);
  CHECK_BYTE(OP_ADD);
  CHECK_BYTE(OP_RETURN);

  PASS();
}

/*

  Check section 11.20 of R6RS spec, it mentions how to determine tail contexts.

  "A tail call is a procedure call that occurs in a tail context."
*/

static void compiles_tail_call_basic() {
  COMPILER_INIT();

  COMPILE("(define (test-func x)"
          "  (next-func x)"
          "  (next-func x))");

  CHECK_SET_FUNC(AS_FUNCTION(out_func->chunk.constants.values[1]));
  CHECK_BYTES(OP_READ_GLOBAL, 0);
  CHECK_BYTES(OP_READ_LOCAL, 1);
  CHECK_CALL(OP_CALL, 1, 0);
  CHECK_BYTE(OP_POP);
  CHECK_BYTES(OP_READ_GLOBAL, 0);
  CHECK_BYTES(OP_READ_LOCAL, 1);
  CHECK_CALL(OP_TAIL_CALL, 1, 0);
  CHECK_BYTE(OP_RETURN);

  // The invocation of `next-func` is not a tail call

  COMPILE("(define (test-func x)"
          "  (+ 1 (next-func x)))");

  CHECK_SET_FUNC(AS_FUNCTION(out_func->chunk.constants.values[1]));
  /* CHECK_BYTES(OP_READ_GLOBAL, 0); */
  /* CHECK_BYTES(OP_CONSTANT, 1); */
  /* CHECK_BYTES(OP_READ_GLOBAL, 2); */
  /* CHECK_BYTES(OP_READ_LOCAL, 1); */
  /* CHECK_CALL(OP_CALL, 1, 0); */
  /* CHECK_CALL(OP_TAIL_CALL, 2, 0); */
  CHECK_BYTES(OP_CONSTANT, 0);
  CHECK_BYTES(OP_READ_GLOBAL, 1);
  CHECK_BYTES(OP_READ_LOCAL, 1);
  CHECK_CALL(OP_CALL, 1, 0);
  CHECK_BYTE(OP_ADD);
  CHECK_BYTE(OP_RETURN);

  PASS();
}

static void compiles_tail_call_begin() {
  COMPILER_INIT();

  COMPILE("(define (test-func)"
          "  (begin"
          "    (next-func x)"
          "    (next-func x)))");

  out_func = AS_FUNCTION(out_func->chunk.constants.values[1]);
  CHECK_BYTES(OP_READ_GLOBAL, 0);
  CHECK_BYTES(OP_READ_GLOBAL, 1);
  CHECK_CALL(OP_CALL, 1, 0);
  CHECK_BYTE(OP_POP);
  CHECK_BYTES(OP_READ_GLOBAL, 0);
  CHECK_BYTES(OP_READ_GLOBAL, 1);
  CHECK_CALL(OP_TAIL_CALL, 1, 0);
  CHECK_BYTE(OP_RETURN);

  // Not a tail call because it's at script scope

  COMPILE("(begin"
          "  (next-func x)"
          "  (next-func x))");
  CHECK_BYTES(OP_READ_GLOBAL, 0);
  CHECK_BYTES(OP_READ_GLOBAL, 1);
  CHECK_CALL(OP_CALL, 1, 0);
  CHECK_BYTE(OP_POP);
  CHECK_BYTES(OP_READ_GLOBAL, 0);
  CHECK_BYTES(OP_READ_GLOBAL, 1);
  CHECK_CALL(OP_CALL, 1, 0);
  CHECK_BYTE(OP_RETURN);

  // `next-func` is not a tail call because the return value of `next-func` is
  // modified before being returned from `test-func`.

  COMPILE("(define (test-func)"
          "  (begin"
          "    (* 1 (next-func x))))");

  out_func = AS_FUNCTION(out_func->chunk.constants.values[1]);
  /* CHECK_BYTES(OP_READ_GLOBAL, 0); */
  /* CHECK_BYTES(OP_CONSTANT, 1); */
  /* CHECK_BYTES(OP_READ_GLOBAL, 2); */
  /* CHECK_BYTES(OP_READ_GLOBAL, 3); */
  /* CHECK_CALL(OP_CALL, 1, 0); */
  /* CHECK_CALL(OP_TAIL_CALL, 2, 0); */
  CHECK_BYTES(OP_CONSTANT, 0);
  CHECK_BYTES(OP_READ_GLOBAL, 1);
  CHECK_BYTES(OP_READ_GLOBAL, 2);
  CHECK_CALL(OP_CALL, 1, 0);
  CHECK_BYTE(OP_MULTIPLY);
  CHECK_BYTE(OP_RETURN);

  PASS();
}

static void compiles_tail_call_let() {
  COMPILER_INIT();

  COMPILE("(let ((x 1))"
          "  (next-func x)"
          "  (next-func x))");

  out_func = AS_FUNCTION(out_func->chunk.constants.values[1]);
  CHECK_BYTES(OP_READ_GLOBAL, 0);
  CHECK_BYTES(OP_READ_LOCAL, 1);
  CHECK_CALL(OP_CALL, 1, 0);
  CHECK_BYTE(OP_POP);
  CHECK_BYTES(OP_READ_GLOBAL, 0);
  CHECK_BYTES(OP_READ_LOCAL, 1);
  CHECK_CALL(OP_TAIL_CALL, 1, 0);
  CHECK_BYTE(OP_RETURN);

  PASS();
}

static void compiles_tail_call_if_expr() {
  COMPILER_INIT();

  COMPILE("(define (test-func x)"
          "  (if (not (equal? x 5))"
          "      (test-func (+ x 1))"
          "      x))");

  out_func = AS_FUNCTION(out_func->chunk.constants.values[1]);
  /* CHECK_BYTES(OP_READ_GLOBAL, 0); */
  /* CHECK_BYTES(OP_READ_GLOBAL, 1); */
  /* CHECK_BYTES(OP_READ_LOCAL, 1); */
  /* CHECK_BYTES(OP_CONSTANT, 2); */
  /* CHECK_CALL(OP_CALL, 2, 0); */
  /* CHECK_CALL(OP_CALL, 1, 0); */
  /* CHECK_JUMP(OP_JUMP_IF_FALSE, 14, 35); */
  /* CHECK_BYTE(OP_POP); */
  /* CHECK_BYTES(OP_READ_GLOBAL, 3); */
  /* CHECK_BYTES(OP_READ_GLOBAL, 4); */
  /* CHECK_BYTES(OP_READ_LOCAL, 1); */
  /* CHECK_BYTES(OP_CONSTANT, 5); */
  /* CHECK_CALL(OP_CALL, 2, 0); */
  /* CHECK_CALL(OP_TAIL_CALL, 1, 0); */
  /* CHECK_JUMP(OP_JUMP, 32, 38); */
  /* CHECK_BYTE(OP_POP); */
  /* CHECK_BYTES(OP_READ_LOCAL, 1); */
  /* CHECK_BYTE(OP_RETURN); */

  CHECK_BYTES(OP_READ_LOCAL, 1);
  CHECK_BYTES(OP_CONSTANT, 0);
  CHECK_BYTE(OP_EQUAL);
  CHECK_BYTE(OP_NOT);
  CHECK_JUMP(OP_JUMP_IF_FALSE, 6, 23);
  CHECK_BYTE(OP_POP);
  CHECK_BYTES(OP_READ_GLOBAL, 1);
  CHECK_BYTES(OP_READ_LOCAL, 1);
  CHECK_BYTES(OP_CONSTANT, 2);
  CHECK_BYTE(OP_ADD);
  CHECK_CALL(OP_TAIL_CALL, 1, 0);
  CHECK_JUMP(OP_JUMP, 20, 26);
  CHECK_BYTE(OP_POP);
  CHECK_BYTES(OP_READ_LOCAL, 1);
  CHECK_BYTE(OP_RETURN);

  PASS();
}

static void compiles_tail_call_nested() {
  COMPILER_INIT();

  COMPILE("(define (test-loop)"
          "  (let loop ((x 1))"
          "    (non-tail-call)"
          "    (begin"
          "      (non-tail-call)"
          "      (if t (non-tail-call)"
          "            (non-tail-call)))"
          "    (let ((y x))"
          "      (loop y)"
          "      (loop x))))");

  CHECK_SET_FUNC(AS_FUNCTION(out_func->chunk.constants.values[1]));
  CHECK_BYTES(OP_CLOSURE, 1);
  CHECK_BYTES(OP_CONSTANT, 0);
  CHECK_CALL(OP_TAIL_CALL, 1, 0);
  CHECK_BYTE(OP_RETURN);

  CHECK_SET_FUNC(AS_FUNCTION(out_func->chunk.constants.values[1]));
  CHECK_BYTES(OP_READ_GLOBAL, 0);
  CHECK_CALL(OP_CALL, 0, 0);
  CHECK_BYTE(OP_POP);
  CHECK_BYTES(OP_READ_GLOBAL, 0);
  CHECK_CALL(OP_CALL, 0, 0);
  CHECK_BYTE(OP_POP);
  CHECK_BYTE(OP_TRUE);
  CHECK_JUMP(OP_JUMP_IF_FALSE, 13, 25);
  CHECK_BYTE(OP_POP);
  CHECK_BYTES(OP_READ_GLOBAL, 0);
  CHECK_CALL(OP_CALL, 0, 0);
  CHECK_JUMP(OP_JUMP, 22, 31);
  CHECK_BYTE(OP_POP);
  CHECK_BYTES(OP_READ_GLOBAL, 0);
  CHECK_CALL(OP_CALL, 0, 0);
  CHECK_BYTE(OP_POP);
  CHECK_BYTES(OP_CLOSURE, 1);
  CHECK_BYTES(1, 0); // Local upvalue 0
  CHECK_BYTES(1, 1); // Local upvalue 1
  CHECK_BYTES(OP_READ_LOCAL, 1);
  CHECK_CALL(OP_TAIL_CALL, 1, 0);
  CHECK_BYTE(OP_RETURN);

  CHECK_SET_FUNC(AS_FUNCTION(out_func->chunk.constants.values[1]));
  CHECK_BYTES(OP_READ_UPVALUE, 0);
  CHECK_BYTES(OP_READ_LOCAL, 1);
  CHECK_CALL(OP_CALL, 1, 0);
  CHECK_BYTE(OP_POP);
  CHECK_BYTES(OP_READ_UPVALUE, 0);
  CHECK_BYTES(OP_READ_UPVALUE, 1);
  CHECK_CALL(OP_TAIL_CALL, 1, 0);
  CHECK_BYTE(OP_RETURN);

  PASS();
}

static void compiler_error_bad_set() {
  COMPILER_INIT();

  COMPILE("(set!)");
  ASSERT_ERROR("set!: Expected binding name");

  COMPILE("(set! foo)");
  ASSERT_ERROR("set!: Expected expression for new value.");

  COMPILE("(set! foo 1 2)");
  ASSERT_ERROR("set!: Expected end of expression.");

  PASS();
}

static void compiler_error_bad_apply() {
  COMPILER_INIT();

  COMPILE("(apply)");
  ASSERT_ERROR("apply: Expected callee expression.");

  COMPILE("(apply foo)");
  ASSERT_ERROR("apply: Expected argument expression.");

  COMPILE("(apply foo bah 2)");
  ASSERT_ERROR("apply: Expected end of expression");

  PASS();
}

static void compiler_error_bad_lambda() {
  COMPILER_INIT();

  COMPILE("(lambda)");
  ASSERT_ERROR("lambda: Expected symbol or parameter list");

  COMPILE("(lambda 3)");
  ASSERT_ERROR("lambda: Expected symbol in parameter list at line 1 in (unknown)");

  COMPILE("(lambda (\"foo\"))");
  ASSERT_ERROR("lambda: Expected symbol in parameter list at line 1 in (unknown)");

  COMPILE("(lambda (foo \"foo\"))");
  ASSERT_ERROR("lambda: Expected symbol in parameter list");

  COMPILE("(lambda (foo . \"foo\"))");
  ASSERT_ERROR("lambda: Expected symbol in parameter list at line 1 in (unknown)");

  COMPILE("(lambda (foo))");
  ASSERT_ERROR("lambda: Expected at least one body expression.");

  PASS();
}

static void compiler_error_bad_define() {
  COMPILER_INIT();

  COMPILE("(define)");
  ASSERT_ERROR("define: Expected symbol or function parameter list");

  COMPILE("(define 3)");
  ASSERT_ERROR("define: Expected symbol or function parameter list at line 1 in (unknown)");

  COMPILE("(define foo)");
  ASSERT_ERROR("define: Expected expression for binding.");

  COMPILE("(define ())");
  ASSERT_ERROR("define: Expected symbol or function parameter list");

  COMPILE("(define (\"foo\"))");
  ASSERT_ERROR("define: Expected function name symbol at line 1 in (unknown)");

  COMPILE("(define (foo \"foo\"))");
  ASSERT_ERROR("define: Expected symbol in parameter list at line 1 in (unknown)");

  COMPILE("(define (foo))");
  ASSERT_ERROR("define: Expected at least one body expression.");

  PASS();
}

static void compiler_error_bad_let() {
  COMPILER_INIT();

  COMPILE("(let)");
  ASSERT_ERROR("let: Expected symbol or binding list");

  COMPILE("(let 2)");
  ASSERT_ERROR("let: Expected binding list at line 1 in (unknown).");

  COMPILE("(let loop)");
  ASSERT_ERROR("let: Expected binding list.");

  COMPILE("(let (x))");
  ASSERT_ERROR("let: Expected binding pair at line 1 in (unknown).");

  COMPILE("(let ((x)))");
  ASSERT_ERROR("let: Expected expression for binding value.");

  COMPILE("(let ((x 1)\n"
          "      (2)))");
  ASSERT_ERROR("let: Expected symbol for binding pair at line 2 in (unknown).");

  PASS();
}

static void compiler_error_bad_define_record() {
  COMPILER_INIT();

  COMPILE("(define-record-type)");
  ASSERT_ERROR("define-record-type: Expected record name.");

  COMPILE("(define-record-type 2)");
  ASSERT_ERROR("define-record-type: Expected record name at line 1 in (unknown).");

  COMPILE("(define-record-type foo)");
  ASSERT_ERROR("define-record-type: Expected 'fields' list.");

  COMPILE("(define-record-type foo 2)");
  ASSERT_ERROR("define-record-type: Expected 'fields' list.");

  COMPILE("(define-record-type foo ())");
  ASSERT_ERROR("define-record-type: Expected 'fields' list.");

  COMPILE("(define-record-type foo (foo))");
  ASSERT_ERROR("define-record-type: Expected 'fields' list at line 1 in (unknown).");

  COMPILE("(define-record-type foo (fields))");
  ASSERT_ERROR("define-record-type: Expected at least one field at line 1 in (unknown).");

  COMPILE("(define-record-type foo (fields 2))");
  ASSERT_ERROR("define-record-type: Expected symbol for field name at line 1 in (unknown).");

  COMPILE("(define-record-type foo (fields name 2))");
  ASSERT_ERROR("define-record-type: Expected symbol for field name at line 1 in (unknown).");

  COMPILE("(define-record-type foo (fields name) whoops)");
  ASSERT_ERROR("define-record-type: Expected end of expression.");

  PASS();
}

static void compiler_error_bad_define_module() {
  COMPILER_INIT();

  COMPILE("(define-module)");
  ASSERT_ERROR("define-module: Expected module specifier (list of symbols).");

  COMPILE("(define-module 2)");
  ASSERT_ERROR(
      "define-module: Expected module specifier (list of symbols) at line 1 in (unknown).");

  COMPILE("(define-module foo)");
  ASSERT_ERROR(
      "define-module: Expected module specifier (list of symbols) at line 1 in (unknown).");

  COMPILE("(define-module ())");
  ASSERT_ERROR(
      "define-module: Expected module specifier (list of symbols) at line 1 in (unknown).");

  COMPILE("(define-module (2))");
  ASSERT_ERROR(
      "define-module: Expected module specifier (list of symbols) at line 1 in (unknown).");

  COMPILE("(define-module (foo 2))");
  ASSERT_ERROR(
      "define-module: Expected module specifier (list of symbols) at line 1 in (unknown).");

  COMPILE("(define-module (foo)\n"
          "  ())");
  ASSERT_ERROR("define-module: Expected 'import' expression.");

  COMPILE("(define-module (foo)\n"
          "  (import))");
  ASSERT_ERROR("define-module: Expected module specifier (list of symbols) after 'import'.");

  COMPILE("(define-module (foo)\n"
          "  (import 2))");
  ASSERT_ERROR(
      "define-module: Expected module specifier (list of symbols) at line 2 in (unknown).");

  COMPILE("(define-module (foo)\n"
          "  (import ()))");
  ASSERT_ERROR(
      "define-module: Expected module specifier (list of symbols) at line 2 in (unknown).");

  COMPILE("(define-module (foo)\n"
          "  (import (2)))");
  ASSERT_ERROR(
      "define-module: Expected module specifier (list of symbols) at line 2 in (unknown).");

  COMPILE("(define-module (foo)\n"
          "  (import (foo)\n"
          "          2))");
  ASSERT_ERROR(
      "define-module: Expected module specifier (list of symbols) at line 3 in (unknown).");

  PASS();
}

static void compiler_suite_cleanup() { mesche_vm_free(&vm); }

void test_compiler_suite() {
  SUITE();

  test_suite_cleanup_func = compiler_suite_cleanup;

  compiles_quoted_exprs();
  compiles_lambda();
  compiles_let();
  compiles_named_let();
  compiles_if_expr();
  compiles_if_expr_no_else();
  compiles_define_attributes();
  compiles_define_simple_binding();
  compiles_define_record_type();

  compiles_and_expr();
  compiles_or_expr();

  compiles_module_import();
  compiles_lambda_rest_args();
  compiles_tail_call_basic();
  compiles_tail_call_begin();
  compiles_tail_call_let();
  compiles_tail_call_if_expr();
  compiles_tail_call_nested();

  compiler_error_bad_set();
  compiler_error_bad_apply();
  compiler_error_bad_lambda();
  compiler_error_bad_define();
  compiler_error_bad_let();
  compiler_error_bad_define_record();
  compiler_error_bad_define_module();

  END_SUITE();
}
