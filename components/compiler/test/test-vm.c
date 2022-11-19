#include "../src/object.h"
#include "../src/value.h"
#include "../src/vm-impl.h"
#include "test.h"

static VM vm;
static Value read_value;

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
  mesche_vm_register_core_modules(&vm, "./modules");

#define VM_EVAL(source, expected_result)                                                           \
  result = mesche_vm_eval_string(&vm, source);                                                     \
  if (result != expected_result) {                                                                 \
    FAIL("Expected interpret result %s, got %d", __stringify(expected_result), result);            \
  }

#define VM_EVAL_FILE(file_path, expected_result)                                                   \
  result = mesche_vm_load_file(&vm, file_path);                                                    \
  if (result != expected_result) {                                                                 \
    FAIL("Expected interpret result %s, got %d", __stringify(expected_result), result);            \
  }

static void returns_basic_values() {
  VM_INIT();
  Value value;

  VM_EVAL("311", INTERPRET_OK);
  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_NUMBER);

  VM_EVAL("#t", INTERPRET_OK);
  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_TRUE);

  VM_EVAL("#f", INTERPRET_OK);
  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_FALSE);

  PASS();
}

static void calls_function_with_rest_args() {
  VM_INIT();
  Value value;

  VM_EVAL("(define-module (test call)\n"
          "  (import (mesche list)\n"
          "          (mesche string)))\n"
          "(define (with-rest x . args)\n"
          "  (car (cdr (cdr args))))\n"
          "(with-rest 1 2 3 4)",
          INTERPRET_OK);

  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_NUMBER);

  /* VM_EVAL("(module-import (mesche list))" */
  /*         "(define (with-rest x :rest args :keys key)" */
  /*         "  (car (cdr (cdr args))))" */
  /*         "(with-rest 4 5 6 7 8)", */
  /*         INTERPRET_OK); */

  /* value = *vm.stack_top; */
  /* ASSERT_KIND(value.kind, VALUE_NUMBER); */

  /* VM_EVAL("(define (with-rest x :rest args :keys key)" */
  /*         "  args)" */
  /*         "(with-rest 4 :key 'foo)", */
  /*         INTERPRET_OK); */

  /* value = *vm.stack_top; */
  /* ASSERT_KIND(value.kind, VALUE_FALSE); */

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

static void evaluates_and_or() {
  VM_INIT();
  Value value;

  VM_EVAL("(or (and #f 2 3)"
          "    (and 3 2 #f)"
          "    (and 2 3 4)"
          "    #f)",
          INTERPRET_OK);
  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_NUMBER);

  if (AS_NUMBER(value) != 4) {
    FAIL("It wasn't 4!");
  }

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

static void evaluates_calls() {
  VM_INIT();
  Value value;

  VM_EVAL("(define (beta x)"
          "  (if (not (equal? x 2))"
          "      0"
          "      x))"
          "(define (alpha x)"
          "  (beta (+ x 1)))"
          "(alpha 1)",
          INTERPRET_OK);
  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_NUMBER);

  if (AS_NUMBER(value) != 2) {
    FAIL("It wasn't 2!");
  }

  PASS();
}

static void evaluates_reset() {
  VM_INIT();
  Value value;

  // Calling `reset` with no calls to `shift` inside will just return the result
  // of the lambda.

  VM_EVAL("(+ 1 (reset (lambda () 3)))", INTERPRET_OK);
  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_NUMBER);

  if (AS_NUMBER(value) != 4) {
    FAIL("It wasn't 4!");
  }

  PASS();
}

static void evaluates_reset_cleanup() {
  VM_INIT();
  Value value;

  // Calling `reset` with no calls to `shift` inside will just return the result
  // of the lambda.

  VM_EVAL("(+ 1 (reset (lambda () (reset (lambda () 3)))))", INTERPRET_OK);
  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_NUMBER);

  if (AS_NUMBER(value) != 4) {
    FAIL("It wasn't 4!");
  }

  PASS();
}

static void evaluates_reset_shift_after_cleanup() {
  VM_INIT();
  Value value;

  VM_EVAL("(+ 1 (reset (lambda () (reset (lambda () 3)) (shift (lambda (k) 4)))))", INTERPRET_OK);
  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_NUMBER);

  if (AS_NUMBER(value) != 5) {
    FAIL("It wasn't 5!");
  }

  PASS();
}

static void evaluates_shift_in_reset() {
  VM_INIT();
  Value value;

  // Calling `shift` without calling the continuation function replaces the result of
  // the `reset`.

  VM_EVAL("(+ 1 (reset (lambda () (* 2 ( + 4 (shift (lambda (k) 3)))))))", INTERPRET_OK);
  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_NUMBER);

  if (AS_NUMBER(value) != 4) {
    FAIL("It wasn't 4!");
  }

  PASS();
}

static void evaluates_shift_in_reset_with_tail_calls() {
  VM_INIT();
  Value value;

  // Calling `shift` without calling the continuation function replaces the result of
  // the `reset`.

  VM_EVAL(
      "(+ 1 (reset (lambda () (* 2 ((lambda () ((lambda () ( + 4 (shift (lambda (k) 3)))))))))))",
      INTERPRET_OK);
  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_NUMBER);

  if (AS_NUMBER(value) != 4) {
    FAIL("It wasn't 4!");
  }

  PASS();
}

static void evaluates_calling_continuation() {
  VM_INIT();
  Value value;

  VM_EVAL("(define (doubler)"
          "  (reset (lambda () (* 2 (shift (lambda (k) k))))))"
          "(+ 2 ((doubler) 3))",
          INTERPRET_OK);
  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_NUMBER);

  if (AS_NUMBER(value) != 8) {
    FAIL("It wasn't 8!");
  }

  PASS();
}

static void evaluates_calling_continuation_with_capture() {
  VM_INIT();
  Value value;

  VM_EVAL("(define (times x)"
          "  (reset (lambda () (* x (shift (lambda (k) k))))))"
          "((lambda (x) (+ x ((times x) 3))) 2)",
          INTERPRET_OK);
  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_NUMBER);

  if (AS_NUMBER(value) != 8) {
    FAIL("It wasn't 8!");
  }

  PASS();
}

static void evaluates_calling_continuation_in_shift() {
  VM_INIT();
  Value value;

  // Calling `shift` and using `k` will evaluate the body of `reset` and return it via
  // the `shift` body.

  VM_EVAL("(+ 1 (reset (lambda () (* 2 (shift (lambda (k) (+ 2 (k 3))))))))", INTERPRET_OK);
  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_NUMBER);

  if (AS_NUMBER(value) != 9) {
    FAIL("It wasn't 9!");
  }

  PASS();
}

static void evaluates_calling_continuation_composed() {
  VM_INIT();
  Value value;

  // In this contorted example, I'm showing that `times` can return its
  // continuation function to later be composed with other functions

  VM_EVAL("(define (times x)"
          "  (reset (lambda () (* x (shift (lambda (k) k))))))"
          "((lambda (x) (+ x ((times x) 3))) 2)",
          INTERPRET_OK);
  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_NUMBER);

  if (AS_NUMBER(value) != 8) {
    FAIL("It wasn't 8!");
  }

  PASS();
}

static void evaluates_continuation_channels_sample() {
  VM_INIT();
  Value value;

  VM_EVAL_FILE("./test/samples/continuations_channels.msc", INTERPRET_OK);
  value = *vm.stack_top;
  ASSERT_KIND(value.kind, VALUE_NUMBER);

  if (AS_NUMBER(value) != 100) {
    FAIL("It wasn't 100!");
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

  evaluates_and_or();
  evaluates_let();
  evaluates_calls();
  evaluates_tail_calls();
  evaluates_tail_calls_named_let();

  evaluates_reset();
  evaluates_reset_cleanup();
  evaluates_reset_shift_after_cleanup();
  evaluates_shift_in_reset();
  evaluates_shift_in_reset_with_tail_calls();
  evaluates_calling_continuation();
  evaluates_calling_continuation_in_shift();
  evaluates_calling_continuation_with_capture();
  evaluates_calling_continuation_composed();
  evaluates_continuation_channels_sample();

  END_SUITE();
}
