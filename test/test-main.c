#include "../src/value.h"
#include "../src/vm.h"
#include "test.h"
#include <stdio.h>

unsigned int tests_passed = 0;
unsigned int tests_skipped = 0;
unsigned int tests_failed = 0;

char fail_message[2048];

TestSuiteCleanup test_suite_cleanup_func = NULL;

void test_mesche_suite() {
  SUITE();

  // Add some space before the Mesche runner output
  printf("\n");

  VM vm;
  mesche_vm_init(&vm, 0, NULL);
  mesche_vm_register_core_modules(&vm, "./modules");
  mesche_vm_load_path_add(&vm, "./test/modules");
  InterpretResult result = mesche_vm_load_file(&vm, "./test/test-mesche.msc");
  if (result != INTERPRET_OK) {
    FAIL("Expected interpret result %s, got %d", __stringify(expected_result), result);
  }
}

int main(void) {
  printf("\n\e[1;36mMesche Test Runner\e[0m\n");

  test_scanner_suite();
  test_compiler_suite();
  test_vm_suite();

  // TODO: Eventually these tests will be invoked by `mesche test`
  test_mesche_suite();

  // Print the test report
  printf("\nTest run complete.\n\n");
  if (tests_passed > 0) {
    printf("\e[1;92m%d passed\e[0m\n", tests_passed);
  }
  if (tests_skipped > 0) {
    printf("\e[1;33m%d skipped\e[0m\n", tests_skipped);
  }
  if (tests_failed > 0) {
    printf("\e[1;91m%d failed\e[0m\n", tests_failed);
  }

  // Final newline for clarity
  printf("\n");

  return tests_failed;
}
