#ifndef mesche_op_h
#define mesche_op_h

typedef enum {
  OP_CONSTANT,
  OP_NIL,
  OP_T,
  OP_POP,
  OP_POP_SCOPE,
  OP_CONS,
  OP_LIST,
  OP_ADD,
  OP_SUBTRACT,
  OP_MULTIPLY,
  OP_DIVIDE,
  OP_NEGATE, // TODO: Remove in favor of SUBTRACT!
  OP_AND,
  OP_OR,
  OP_NOT,
  OP_EQV,
  OP_EQUAL,
  OP_LOAD_FILE,
  OP_DEFINE_RECORD,
  OP_DEFINE_MODULE,
  OP_RESOLVE_MODULE,
  OP_IMPORT_MODULE,
  OP_ENTER_MODULE,
  OP_EXPORT_SYMBOL,
  OP_DEFINE_GLOBAL,
  OP_SET_GLOBAL,
  OP_SET_UPVALUE,
  OP_SET_LOCAL,
  OP_READ_GLOBAL,
  OP_READ_UPVALUE,
  OP_READ_LOCAL,
  OP_JUMP,
  OP_JUMP_IF_FALSE,
  OP_CALL,
  OP_CLOSURE,
  OP_CLOSE_UPVALUE,
  OP_DISPLAY,
  OP_RETURN
} MescheOpCode;

#endif
