#ifndef mesche_scanner_h
#define mesche_scanner_h

#include "io.h"
#include "vm.h"

#define SCANNER_BUFFER_MAX 1024

typedef enum {
  TokenKindNone,
  TokenKindLeftParen,
  TokenKindRightParen,
  TokenKindDot,
  TokenKindList,
  TokenKindCons,
  TokenKindQuote,
  TokenKindQuoteChar,
  TokenKindBackquote,
  TokenKindUnquote,
  TokenKindSplice,
  TokenKindFalse,
  TokenKindTrue,
  TokenKindCharacter,
  TokenKindString,
  TokenKindSymbol,
  TokenKindKeyword,
  TokenKindNumber,
  TokenKindPlus,
  TokenKindMinus,
  TokenKindStar,
  TokenKindSlash,
  TokenKindPercent,
  TokenKindAnd,
  TokenKindOr,
  TokenKindNot,
  TokenKindEqv,
  TokenKindEqual,
  TokenKindGreaterThan,
  TokenKindGreaterEqual,
  TokenKindLessThan,
  TokenKindLessEqual,
  TokenKindDefine,
  TokenKindDefineModule,
  TokenKindDefineRecordType,
  TokenKindModuleEnter,
  TokenKindModuleImport,
  TokenKindImport,
  TokenKindLoadFile,
  TokenKindSet,
  TokenKindBegin,
  TokenKindLet,
  TokenKindIf,
  TokenKindLambda,
  TokenKindApply,
  TokenKindReset,
  TokenKindShift,
  TokenKindBreak,
  TokenKindDisplay,
  TokenKindError,
  TokenKindEOF
} TokenKind;

typedef struct {
  const char *start;
  VM *vm;
  MeschePort *port;
  char buffer[SCANNER_BUFFER_MAX];
  char peeks[2];
  int count;
  int line;
  int column;
  const char *file_name;
} Scanner;

typedef struct {
  TokenKind kind;
  TokenKind sub_kind;
  const char *start;
  int length;
  int line;
} Token;

void mesche_scanner_init(Scanner *scanner, VM *vm, MeschePort *port);
Token mesche_scanner_next_token(Scanner *scanner);

#endif
