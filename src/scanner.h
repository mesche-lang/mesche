#ifndef mesche_scanner_h
#define mesche_scanner_h

typedef enum {
  TokenKindNone,
  TokenKindLeftParen,
  TokenKindRightParen,
  TokenKindList,
  TokenKindCons,
  TokenKindQuote,
  TokenKindBackquote,
  TokenKindUnquote,
  TokenKindSplice,
  TokenKindNil,
  TokenKindTrue,
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
  TokenKindReset,
  TokenKindShift,
  TokenKindDisplay,
  TokenKindError,
  TokenKindEOF
} TokenKind;

typedef struct {
  const char *start;
  const char *current;
  int line;
} Scanner;

typedef struct {
  TokenKind kind;
  TokenKind sub_kind;
  const char *start;
  int length;
  int line;
} Token;

void mesche_scanner_init(Scanner *scanner, const char *source);
Token mesche_scanner_next_token(Scanner *scanner);

#endif
