#ifndef mesche_disasm_h
#define mesche_disasm_h

#include "chunk.h"

int mesche_disasm_simple_instr(MeschePort *port, const char *name, int offset);
int mesche_disasm_const_instr(MeschePort *port, const char *name, Chunk *chunk, int offset);
int mesche_disasm_instr(MeschePort *port, Chunk *chunk, int offset);
void mesche_disasm_function(MeschePort *port, ObjectFunction *function);

#endif
