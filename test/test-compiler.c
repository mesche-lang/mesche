#include "../src/compiler.h"
#include "../src/object.h"
#include "../src/op.h"
#include "../src/vm.h"
#include "test.h"

static VM vm;
static ObjectFunction *out_func;
static int byte_idx = 0;

#define COMPILER_INIT()                                                                            \
  InterpretResult result;                                                                          \
  mesche_vm_init(&vm, 0, NULL);

#define COMPILE(source)                                                                            \
  byte_idx = 0;                                                                                    \
  out_func = mesche_compile_source(&vm, source);

#define CHECK_BYTE(byte)                                                                           \
  if (out_func->chunk.code[byte_idx] != byte) {                                                    \
    FAIL("Expected byte %s, got %d", __stringify(byte), out_func->chunk.code[byte_idx]);           \
  }                                                                                                \
  byte_idx++;

#define CHECK_BYTES(byte1, byte2)                                                                  \
  CHECK_BYTE(byte1);                                                                               \
  CHECK_BYTE(byte2);

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

static void compiler_suite_cleanup() { mesche_vm_free(&vm); }

void test_compiler_suite() {
  SUITE();

  test_suite_cleanup_func = compiler_suite_cleanup;

  compiles_module_import();

  END_SUITE();
}
