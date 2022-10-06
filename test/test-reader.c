#include "../src/object.h"
#include "../src/reader.h"
#include "../src/scanner.h"
#include "../src/vm-impl.h"
#include "test.h"

static VM vm;
static Reader reader;

#define READER_INIT(source)                                                                        \
  ObjectSyntax *current_syntax;                                                                    \
  InterpretResult result;                                                                          \
  mesche_vm_init(&vm, 0, NULL);                                                                    \
  mesche_reader_from_string(&vm.reader_context, &reader, source);

#define READ_NEXT()                                                                                \
  current_syntax = mesche_reader_read_next(&reader);                                               \
  if (IS_UNSPECIFIED(current_syntax->value)) {                                                     \
    FAIL("Reader could not parse input!");                                                         \
  }

#define READ_STRING(expected_str)                                                                  \
  READ_NEXT();                                                                                     \
  if (memcmp(AS_CSTRING(current_syntax->value), expected_str, strlen(expected_str)) != 0) {        \
    FAIL("Expected string %s, got %s", expected_str, AS_CSTRING(current_syntax->value));           \
  }

#define ASSERT_NUMBER(value, number)                                                               \
  if (AS_NUMBER(value) != number) {                                                                \
    FAIL("Expected number %lf, got %lf", number, AS_NUMBER(value));                                \
  }

#define ASSERT_SYMBOL(value, symbol_text)                                                          \
  if (IS_SYMBOL(value)) {                                                                          \
    ObjectSymbol *symbol = AS_SYMBOL(value);                                                       \
    if (symbol->name->length != strlen(symbol_text) ||                                             \
        memcmp(symbol->name->chars, symbol_text, symbol->name->length) != 0) {                     \
      FAIL("Expected symbol %s, got %s", symbol_text, AS_SYMBOL(value)->name->chars);              \
    }                                                                                              \
  } else {                                                                                         \
    FAIL("Expected symbol, got %d", AS_OBJECT(value)->kind);                                       \
  }

#define READ_SYMBOL(source)                                                                        \
  READ_NEXT();                                                                                     \
  ASSERT_SYMBOL(current_syntax->value, source);

#define EXPECT_LIST()                                                                              \
  if (!IS_CONS(current_syntax->value)) {                                                           \
    FAIL("Expected a cons, got %d", AS_OBJECT(current_syntax->value)->kind);                       \
  }

#define EXPECT_SUB_LIST()                                                                          \
  if (!IS_CONS(AS_SYNTAX(AS_CONS(current_syntax->value)->car)->value)) {                           \
    FAIL("Expected a cons, got %d", AS_OBJECT(current_syntax->value)->kind);                       \
  } else {                                                                                         \
    current_syntax = AS_SYNTAX(AS_CONS(current_syntax->value)->car);                               \
  }

#define EXPECT_SYMBOL(symbol_text)                                                                 \
  ASSERT_SYMBOL(AS_SYNTAX(AS_CONS(current_syntax->value)->car)->value, symbol_text);               \
  current_syntax = AS_SYNTAX(AS_CONS(current_syntax->value)->cdr);

#define EXPECT_NUMBER(number)                                                                      \
  ASSERT_NUMBER(AS_SYNTAX(AS_CONS(current_syntax->value)->car)->value, number);                    \
  current_syntax = AS_SYNTAX(AS_CONS(current_syntax->value)->cdr);

#define EXPECT_EMPTY()                                                                             \
  if (!IS_EMPTY(AS_CONS(current_syntax->value)->car)) {                                            \
    FAIL("Expected an empty list, got %d", current_syntax->value.kind);                            \
  } else {                                                                                         \
    current_syntax = AS_SYNTAX(AS_CONS(current_syntax->value)->cdr);                               \
  }

static void reads_escaped_strings() {
  READER_INIT("\"ab\" "
              "\"a\\nb\" "
              "\"a\\tb\" "
              "\"a\\\\b\" ");

  // Need to double-escape these because we're typing them as C string literals.
  // In a real source file, they won't be escaped like this.
  READ_STRING("ab");
  READ_STRING("a\nb");
  READ_STRING("a\tb");
  READ_STRING("a\\b");

  PASS();
}

static void reads_symbols() {
  READER_INIT("some-func "
              "< "
              ">= "
              "+ "
              ". "
              "... ");

  READ_SYMBOL("some-func");
  READ_SYMBOL("<");
  READ_SYMBOL(">=");
  READ_SYMBOL("+");
  READ_SYMBOL(".");
  /* READ_SYMBOL("..."); */

  PASS();
}

static void reads_lists_and_pairs() {
  READER_INIT("(1 . 2)"
              "(1 2 3)"
              "(+ 1 2 (* 3 4))"
              "(+ (/ 1 2) (* (- 5 2) 4))"
              "()");

  READ_NEXT();
  ObjectCons *cons = AS_CONS(current_syntax->value);
  if (!IS_CONS(current_syntax->value) || !IS_SYNTAX(cons->car) ||
      !IS_NUMBER(AS_SYNTAX(cons->car)->value) || !IS_SYNTAX(cons->cdr) ||
      !IS_NUMBER(AS_SYNTAX(cons->cdr)->value)) {
    FAIL("Result was not a cons pair of two numbers!");
  }

  READ_NEXT();
  EXPECT_LIST();
  EXPECT_NUMBER(1.f);
  EXPECT_NUMBER(2.f);
  EXPECT_NUMBER(3.f);

  READ_NEXT();
  EXPECT_LIST();
  EXPECT_SYMBOL("+");
  EXPECT_NUMBER(1.f);
  EXPECT_NUMBER(2.f);
  EXPECT_SUB_LIST();
  EXPECT_SYMBOL("*");
  EXPECT_NUMBER(3.f);
  EXPECT_NUMBER(4.f);

  READ_NEXT();
  EXPECT_LIST();
  EXPECT_SYMBOL("+");
  ObjectCons *parent = AS_CONS(current_syntax->value);
  EXPECT_SUB_LIST();
  EXPECT_SYMBOL("/");
  EXPECT_NUMBER(1.f);
  EXPECT_NUMBER(2.f);
  current_syntax = AS_SYNTAX(parent->cdr);
  parent = AS_CONS(current_syntax->value);
  EXPECT_SUB_LIST();
  parent = AS_CONS(current_syntax->value);
  EXPECT_SYMBOL("*");
  EXPECT_SUB_LIST();
  EXPECT_SYMBOL("-");
  EXPECT_NUMBER(5.f);
  EXPECT_NUMBER(2.f);
  current_syntax = AS_SYNTAX(AS_CONS(AS_SYNTAX(parent->cdr)->value)->cdr);
  EXPECT_NUMBER(4.f);

  READ_NEXT();
  if (!IS_EMPTY(current_syntax->value)) {
    FAIL("Result was not empty list!");
  }

  PASS();
}

static void reads_quoted_expressions() {
  READER_INIT("'symbol"
              "'(+ 1 2 (* 3 4))"
              "'(quote a)"
              "'()");

  READ_NEXT();
  EXPECT_LIST();
  EXPECT_SYMBOL("quote");
  EXPECT_SYMBOL("symbol");

  READ_NEXT();
  EXPECT_LIST();
  EXPECT_SYMBOL("quote");
  EXPECT_SUB_LIST();
  EXPECT_SYMBOL("+");
  EXPECT_NUMBER(1.f);
  EXPECT_NUMBER(2.f);
  EXPECT_SUB_LIST();
  EXPECT_SYMBOL("*");
  EXPECT_NUMBER(3.f);
  EXPECT_NUMBER(4.f);

  READ_NEXT();
  EXPECT_LIST();
  EXPECT_SYMBOL("quote");
  EXPECT_SUB_LIST();
  EXPECT_SYMBOL("quote");
  EXPECT_SYMBOL("a");

  READ_NEXT();
  EXPECT_LIST();

  // ':keyword -> (quote :keyword)

  PASS();
}

static void reader_suite_cleanup() { mesche_vm_free(&vm); }

void test_reader_suite() {
  SUITE();

  test_suite_cleanup_func = reader_suite_cleanup;

  reads_escaped_strings();
  reads_symbols();
  reads_lists_and_pairs();
  reads_quoted_expressions();
}
