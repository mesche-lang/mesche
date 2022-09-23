#ifndef __TEST_H
#define __TEST_H

#include <stdio.h>

typedef void (*TestSuiteCleanup)(void);

extern unsigned int tests_passed;
extern unsigned int tests_skipped;
extern unsigned int tests_failed;
extern TestSuiteCleanup test_suite_cleanup_func;

extern char fail_message[2048];

#define SUITE() printf("\n\n\e[1;33m • SUITE\e[0m %s\n", __func__);

#define END_SUITE() test_suite_cleanup_func = NULL;

#define PASS()                                                                                     \
  tests_passed++;                                                                                  \
  printf("\e[1;92m  ✓ PASS\e[0m %s\n", __func__);                                                  \
  if (test_suite_cleanup_func) {                                                                   \
    test_suite_cleanup_func();                                                                     \
  }                                                                                                \
  return;

#define SKIP()                                                                                     \
  tests_skipped++;                                                                                 \
  printf("\e[1;33m ᠅ SKIP\e[0m %s\n", __func__);                                                   \
  return;

#define FAIL(message, ...)                                                                         \
  tests_failed++;                                                                                  \
  printf("\e[1;91m  ✗ FAIL\e[0m %s\n", __func__);                                                  \
  sprintf(fail_message, message, ##__VA_ARGS__);                                                   \
  printf("      %s\n\n", fail_message);                                                            \
  if (test_suite_cleanup_func) {                                                                   \
    test_suite_cleanup_func();                                                                     \
  }                                                                                                \
  return;

#define ASSERT_INT(expected, actual)                                                               \
  if (actual != expected) {                                                                        \
    FAIL("Expected integer: %ld\n                   got: %ld\n               at line: %d\n",       \
         (long int)expected, (long int)actual, __LINE__);                                          \
  }

#define str(s) #s
#define __stringify(s) str(s)

void test_scanner_suite(void);
void test_compiler_suite(void);
void test_vm_suite(void);

#endif
