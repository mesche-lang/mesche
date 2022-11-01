#ifndef mesche_scanner_h
#define mesche_scanner_h

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
  const char *current;
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

void mesche_scanner_init(Scanner *scanner, const char *source);
Token mesche_scanner_next_token(Scanner *scanner);

#endif
