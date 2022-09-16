#include "../src/scanner.h"
#include "test.h"

#define INIT_SCANNER(source)                                                                       \
  Scanner test_scanner;                                                                            \
  Token next_token;                                                                                \
  mesche_scanner_init(&test_scanner, source);

#define ASSERT_KIND(actual_kind, expected_kind)                                                    \
  if (actual_kind != expected_kind) {                                                              \
    FAIL("Expected kind %s, got %d", __stringify(expected_kind), actual_kind);                     \
  }

#define CHECK_TOKEN(_kind)                                                                         \
  next_token = mesche_scanner_next_token(&test_scanner);                                           \
  ASSERT_KIND(next_token.kind, _kind);

#define CHECK_SYMBOL(_sub_kind)                                                                    \
  next_token = mesche_scanner_next_token(&test_scanner);                                           \
  ASSERT_KIND(next_token.kind, TokenKindSymbol);                                                   \
  ASSERT_KIND(next_token.sub_kind, _sub_kind);

static void scanner_finds_literals() {
  INIT_SCANNER("t nil -1 2.2 \"three\"");
  CHECK_TOKEN(TokenKindTrue);
  CHECK_TOKEN(TokenKindNil);
  CHECK_TOKEN(TokenKindNumber);
  CHECK_TOKEN(TokenKindNumber);
  CHECK_TOKEN(TokenKindString);

  CHECK_TOKEN(TokenKindEOF);
  PASS();
}

static void scanner_finds_lists_and_symbols() {
  INIT_SCANNER("(list :one \"two\" 'three)");
  CHECK_TOKEN(TokenKindLeftParen);
  CHECK_SYMBOL(TokenKindList);
  CHECK_TOKEN(TokenKindKeyword);
  CHECK_TOKEN(TokenKindString);
  CHECK_TOKEN(TokenKindQuote);
  CHECK_TOKEN(TokenKindSymbol);
  CHECK_TOKEN(TokenKindRightParen);

  CHECK_TOKEN(TokenKindEOF);
  PASS();
}

static void scanner_finds_operations() {
  INIT_SCANNER("+ - / * and or eqv? equal?");
  CHECK_TOKEN(TokenKindPlus);
  CHECK_SYMBOL(TokenKindMinus);
  CHECK_TOKEN(TokenKindSlash);
  CHECK_TOKEN(TokenKindStar);
  CHECK_SYMBOL(TokenKindAnd);
  CHECK_SYMBOL(TokenKindOr);
  CHECK_SYMBOL(TokenKindEqv);
  CHECK_SYMBOL(TokenKindEqual);

  CHECK_TOKEN(TokenKindEOF);
  PASS();
}

static void scanner_matches_exact_tokens() {
  INIT_SCANNER("let letter list listed define-module define-modulerama");
  CHECK_SYMBOL(TokenKindLet);
  CHECK_SYMBOL(TokenKindNone);
  CHECK_SYMBOL(TokenKindList);
  CHECK_SYMBOL(TokenKindNone);
  CHECK_SYMBOL(TokenKindDefineModule);
  CHECK_SYMBOL(TokenKindNone);

  CHECK_TOKEN(TokenKindEOF);
  PASS();
}

static void scanner_finds_distinguishes_operators() {
  INIT_SCANNER(">= > <= < <class> >fish <- -> - -- % %internal");
  CHECK_SYMBOL(TokenKindGreaterEqual);
  CHECK_SYMBOL(TokenKindGreaterThan);
  CHECK_SYMBOL(TokenKindLessEqual);
  CHECK_SYMBOL(TokenKindLessThan);
  CHECK_TOKEN(TokenKindSymbol);
  CHECK_TOKEN(TokenKindSymbol);
  CHECK_TOKEN(TokenKindSymbol);
  CHECK_TOKEN(TokenKindSymbol);
  CHECK_SYMBOL(TokenKindMinus);
  CHECK_TOKEN(TokenKindSymbol);
  CHECK_SYMBOL(TokenKindPercent);
  CHECK_TOKEN(TokenKindSymbol);

  CHECK_TOKEN(TokenKindEOF);
  PASS();
}

void test_scanner_suite() {
  SUITE();

  scanner_finds_literals();
  scanner_finds_lists_and_symbols();
  scanner_finds_operations();
  scanner_matches_exact_tokens();
  scanner_finds_distinguishes_operators();
}
