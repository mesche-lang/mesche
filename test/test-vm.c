#include "../src/object.h"
#include "../src/value.h"
#include "../src/vm.h"
#include "test.h"

static VM vm;

#define ASSERT_KIND(actual_kind, expected_kind)                                                    \
  if (actual_kind != expected_kind) {                                                              \
    FAIL("Expected kind %s, got %d", __stringify(expected_kind), actual_kind);                     \
  }

#define ASSERT_OBJECT(_value, expected_kind)                                                       \
  if (AS_OBJECT(_value)->kind != expected_kind) {                                                  \
    FAIL("Expected kind %s, got %d", __stringify(expected_kind), AS_OBJECT(_value)->kind);         \
  }

#define VM_INIT()                                                                                  \
  InterpretResult result;                                                                          \
  mesche_vm_init(&vm, 0, NULL);                                                                    \
  mesche_vm_load_path_add(&vm, "./test/modules");                                                  \
  mesche_vm_register_core_modules(&vm);

#define VM_EVAL(source, expected_result)                                                           \
  result = mesche_vm_eval_string(&vm, source);                                                     \
  if (result != expected_result) {                                                                 \
    FAIL("Expected interpret result %s, got %d", __stringify(expected_result), result);            \
  }

static void returns_basic_values() {
  VM_INIT();
  Value value;

  VM_EVAL("311", INTERPRET_OK);
  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_NUMBER);

  VM_EVAL("t", INTERPRET_OK);
  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_TRUE);

  VM_EVAL("nil", INTERPRET_OK);
  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_NIL);

  PASS();
}

static void calls_function_with_rest_args() {
  VM_INIT();
  Value value;

  VM_EVAL("(module-import (mesche list))"
          "(define (with-rest x :rest args)"
          "  (car (cdr (cdr args))))"
          "(with-rest 1 2 3 4)", INTERPRET_OK);

  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_NUMBER);

  VM_EVAL("(module-import (mesche list))"
          "(define (with-rest x :rest args :keys key)"
          "  (car (cdr (cdr args))))"
          "(with-rest 4 5 6 7 8)", INTERPRET_OK);

  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_NUMBER);

  VM_EVAL("(define (with-rest x :rest args :keys key)"
          "  args)"
          "(with-rest 4 :key 'foo)", INTERPRET_OK);

  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_NIL);

  PASS();
}

static void imports_modules() {
  VM_INIT();
  Value value;

  VM_EVAL("(module-import (test alpha)) "
          "(hello)",
          INTERPRET_OK);
  value = *vm.stack_top;
  ASSERT_OBJECT(value, ObjectKindString);

  PASS();
}

static void evaluates_let() {
  VM_INIT();
  Value value;

  VM_EVAL("(let ((x 3) (y 4))"
          "  (+ x y))",
          INTERPRET_OK);
  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_NUMBER);

  if (AS_NUMBER(value) != 7) {
    FAIL("It wasn't 7!");
  }

  PASS();
}

static void evaluates_tail_calls() {
  VM_INIT();
  Value value;

  VM_EVAL("(define (loop x)"
          "  (if (not (equal? x 5))"
          "      (loop (+ x 1))"
          "      x))"
          "(loop 1)",
          INTERPRET_OK);
  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_NUMBER);

  if (AS_NUMBER(value) != 5) {
    FAIL("It wasn't 5!");
  }

  PASS();
}

static void evaluates_tail_calls_named_let() {
  VM_INIT();
  Value value;

  VM_EVAL("(let loop ((x 1))"
          "  (if (not (equal? x 5))"
          "      (loop (+ x 1))"
          "      x))",
          INTERPRET_OK);
  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_NUMBER);

  if (AS_NUMBER(value) != 5) {
    FAIL("It wasn't 5!");
  }

  PASS();
}

static void vm_suite_cleanup() { mesche_vm_free(&vm); }

void test_vm_suite() {
  SUITE();

  test_suite_cleanup_func = vm_suite_cleanup;

  returns_basic_values();
  calls_function_with_rest_args();
  imports_modules();
  evaluates_let();
  evaluates_tail_calls();
  evaluates_tail_calls_named_let();

  END_SUITE();
}
