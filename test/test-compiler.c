#include "../src/compiler.h"
#include "../src/object.h"
#include "../src/op.h"
#include "../src/vm.h"
#include "test.h"

static VM vm;
static ObjectFunction *out_func;
static int byte_idx = 0;
static uint16_t jump_val;

#define COMPILER_INIT()                                                        \
  InterpretResult result;                                                      \
  mesche_vm_init(&vm, 0, NULL);

#define COMPILE(source)                                                        \
  byte_idx = 0;                                                                \
  out_func = mesche_compile_source(&vm, source);                               \
  if (out_func == NULL) {                                                      \
    FAIL("Compilation failed!");                                               \
  }

#define CHECK_BYTE(byte)                                                       \
  if (out_func->chunk.code[byte_idx] != byte) {                                \
    FAIL("Expected byte %s, got %d", __stringify(byte),                        \
         out_func->chunk.code[byte_idx]);                                      \
  }                                                                            \
  byte_idx++;

#define CHECK_BYTES(byte1, byte2)                                              \
  CHECK_BYTE(byte1);                                                           \
  CHECK_BYTE(byte2);

#define CHECK_JUMP(instr, source, dest)                                        \
  CHECK_BYTE(instr);                                                           \
  byte_idx += 2;

#define CHECK_CALL(arg_count, keyword_count)                                   \
  CHECK_BYTE(OP_CALL);                                                         \
  CHECK_BYTE(arg_count);                                                       \
  CHECK_BYTE(keyword_count);

/* jump_val = (uint16_t)(out_func->chunk.code[byte_idx + 1] << 8); \ */
/* jump_val |= out_func->chunk.code[byte_idx + 2]; \ */
/* ASSERT_INT(byte_idx, offset + 3 + sign * jump_val); */

static void compiles_module_import() {
  COMPILER_INIT();

  COMPILE("(module-import (test alpha))");

  CHECK_BYTES(OP_CONSTANT, 0);
  CHECK_BYTES(OP_CONSTANT, 1);
  CHECK_BYTES(OP_LIST, 2);
  CHECK_BYTE(OP_RESOLVE_MODULE);
  CHECK_BYTE(OP_IMPORT_MODULE);
  CHECK_BYTE(OP_RETURN);

  PASS();
}

static void compiles_let() {
  COMPILER_INIT();

  COMPILE("(let ((x 3) (y 4))"
          "  (+ x y))");

  CHECK_BYTES(OP_CONSTANT, 0);
  CHECK_BYTES(OP_SET_LOCAL, 1);
  CHECK_BYTES(OP_CONSTANT, 1);
  CHECK_BYTES(OP_SET_LOCAL, 2);
  CHECK_BYTES(OP_READ_LOCAL, 1);
  CHECK_BYTES(OP_READ_LOCAL, 2);
  CHECK_BYTE(OP_ADD);
  CHECK_BYTES(OP_POP_SCOPE, 1);
  CHECK_BYTES(OP_POP_SCOPE, 1);
  CHECK_BYTE(OP_RETURN);

  PASS();
}

static void compiles_named_let() {
  COMPILER_INIT();

  COMPILE("(let test-let ((i 1))"
          "  (if (equal? i 5)"
          "      (test-let (+ i 1))"
          "      i))");

  CHECK_BYTES(OP_CONSTANT, 0);
  CHECK_BYTES(OP_SET_LOCAL, 2);
  CHECK_BYTES(OP_READ_LOCAL, 2);
  CHECK_BYTES(OP_CONSTANT, 1);
  CHECK_BYTE(OP_EQUAL);
  CHECK_JUMP(OP_JUMP_IF_FALSE, 9, 26); // TODO: Implement this check
  CHECK_BYTE(OP_POP);
  CHECK_BYTES(OP_READ_LOCAL, 1);
  CHECK_BYTES(OP_READ_LOCAL, 2);
  CHECK_BYTES(OP_CONSTANT, 2);
  CHECK_BYTE(OP_ADD);
  CHECK_CALL(1, 0);
  CHECK_JUMP(OP_JUMP, 23, 29);
  CHECK_BYTE(OP_POP);
  CHECK_BYTES(OP_READ_LOCAL, 2);
  CHECK_BYTES(OP_POP_SCOPE, 1);
  CHECK_BYTES(OP_POP_SCOPE, 1);
  CHECK_BYTE(OP_RETURN);

  PASS();
}

static void compiler_suite_cleanup() { mesche_vm_free(&vm); }

void test_compiler_suite() {
  SUITE();

  test_suite_cleanup_func = compiler_suite_cleanup;

  /* compiles_module_import(); */
  /* compiles_let(); */
  compiles_named_let();

  END_SUITE();
}
