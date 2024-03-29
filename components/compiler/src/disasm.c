#include <stdio.h>

#include "chunk.h"
#include "function.h"
#include "object.h"
#include "op.h"
#include "port.h"
#include "value.h"

int mesche_disasm_simple_instr(MeschePort *port, const char *name, int offset) {
  fprintf(port->data.file.fp, "%s\n", name);
  return offset + 1;
}

int mesche_disasm_const_instr(MeschePort *port, const char *name, Chunk *chunk, int offset) {
  uint8_t constant = chunk->code[offset + 1];
  fprintf(port->data.file.fp, "%-16s %4d  '", name, constant);
  fflush(stdout);
  mesche_value_print(port, chunk->constants.values[constant]);
  fprintf(port->data.file.fp, "'\n");

  return offset + 2;
}

int mesche_disasm_byte_instr(MeschePort *port, const char *name, Chunk *chunk, int offset) {
  uint8_t slot = chunk->code[offset + 1];
  fprintf(port->data.file.fp, "%-16s %4d\n", name, slot);
  return offset + 2;
}

int mesche_disasm_bytes_instr(MeschePort *port, const char *name, Chunk *chunk, int offset) {
  uint8_t slot = chunk->code[offset + 1];
  uint8_t slot2 = chunk->code[offset + 2];
  fprintf(port->data.file.fp, "%-16s %4d %4d\n", name, slot, slot2);
  return offset + 3;
}

int mesche_disasm_jump_instr(MeschePort *port, const char *name, int sign, Chunk *chunk,
                             int offset) {
  uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
  jump |= chunk->code[offset + 2];
  fprintf(port->data.file.fp, "%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
  return offset + 3;
}

int mesche_disasm_instr(MeschePort *port, Chunk *chunk, int offset) {
  fprintf(port->data.file.fp, "%04d ", offset);
  if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
    fprintf(port->data.file.fp, "  |   ");
  } else {
    fprintf(port->data.file.fp, "%4d  ", chunk->lines[offset]);
  }

  uint8_t instr = chunk->code[offset];
  switch (instr) {
  case OP_NOP:
    return mesche_disasm_simple_instr(port, "OP_NOP", offset);
  case OP_CONSTANT:
    return mesche_disasm_const_instr(port, "OP_CONSTANT", chunk, offset);
  case OP_FALSE:
    return mesche_disasm_simple_instr(port, "OP_FALSE", offset);
  case OP_TRUE:
    return mesche_disasm_simple_instr(port, "OP_TRUE", offset);
  case OP_LIST:
    return mesche_disasm_byte_instr(port, "OP_LIST", chunk, offset);
  case OP_POP:
    return mesche_disasm_simple_instr(port, "OP_POP", offset);
  case OP_POP_SCOPE:
    return mesche_disasm_byte_instr(port, "OP_POP_SCOPE", chunk, offset);
  case OP_CONS:
    return mesche_disasm_simple_instr(port, "OP_CONS", offset);
  case OP_ADD:
    return mesche_disasm_simple_instr(port, "OP_ADD", offset);
  case OP_SUBTRACT:
    return mesche_disasm_simple_instr(port, "OP_SUBTRACT", offset);
  case OP_MULTIPLY:
    return mesche_disasm_simple_instr(port, "OP_MULTIPLY", offset);
  case OP_DIVIDE:
    return mesche_disasm_simple_instr(port, "OP_DIVIDE", offset);
  case OP_MODULO:
    return mesche_disasm_simple_instr(port, "OP_MODULO", offset);
  case OP_NEGATE:
    return mesche_disasm_simple_instr(port, "OP_NEGATE", offset);
  case OP_NOT:
    return mesche_disasm_simple_instr(port, "OP_NOT", offset);
  case OP_GREATER_THAN:
    return mesche_disasm_simple_instr(port, "OP_GREATER_THAN", offset);
  case OP_GREATER_EQUAL:
    return mesche_disasm_simple_instr(port, "OP_GREATER_EQUAL", offset);
  case OP_LESS_THAN:
    return mesche_disasm_simple_instr(port, "OP_LESS_THAN", offset);
  case OP_LESS_EQUAL:
    return mesche_disasm_simple_instr(port, "OP_LESS_EQUAL", offset);
  case OP_EQV:
    return mesche_disasm_simple_instr(port, "OP_EQV", offset);
  case OP_EQUAL:
    return mesche_disasm_simple_instr(port, "OP_EQUAL", offset);
  case OP_RETURN:
    return mesche_disasm_simple_instr(port, "OP_RETURN", offset);
  case OP_APPLY:
    return mesche_disasm_simple_instr(port, "OP_APPLY", offset);
  case OP_RESET:
    return mesche_disasm_simple_instr(port, "OP_RESET", offset);
  case OP_SHIFT:
    return mesche_disasm_simple_instr(port, "OP_SHIFT", offset);
  case OP_REIFY:
    return mesche_disasm_simple_instr(port, "OP_REIFY", offset);
  case OP_BREAK:
    return mesche_disasm_simple_instr(port, "OP_BREAK", offset);
  case OP_DISPLAY:
    return mesche_disasm_simple_instr(port, "OP_DISPLAY", offset);
  case OP_LOAD_FILE:
    return mesche_disasm_simple_instr(port, "OP_LOAD_FILE", offset);
  case OP_DEFINE_RECORD:
    return mesche_disasm_byte_instr(port, "OP_DEFINE_RECORD", chunk, offset);
  case OP_DEFINE_MODULE:
    return mesche_disasm_simple_instr(port, "OP_DEFINE_MODULE", offset);
  case OP_RESOLVE_MODULE:
    return mesche_disasm_simple_instr(port, "OP_RESOLVE_MODULE", offset);
  case OP_IMPORT_MODULE:
    return mesche_disasm_simple_instr(port, "OP_IMPORT_MODULE", offset);
  case OP_ENTER_MODULE:
    return mesche_disasm_simple_instr(port, "OP_ENTER_MODULE", offset);
  case OP_EXPORT_SYMBOL:
    return mesche_disasm_const_instr(port, "OP_EXPORT_SYMBOL", chunk, offset);
  case OP_DEFINE_GLOBAL:
    return mesche_disasm_const_instr(port, "OP_DEFINE_GLOBAL", chunk, offset);
  case OP_READ_GLOBAL:
    return mesche_disasm_const_instr(port, "OP_READ_GLOBAL", chunk, offset);
  case OP_READ_UPVALUE:
    return mesche_disasm_byte_instr(port, "OP_READ_UPVALUE", chunk, offset);
  case OP_SET_UPVALUE:
    return mesche_disasm_byte_instr(port, "OP_SET_UPVALUE", chunk, offset);
  case OP_SET_GLOBAL:
    return mesche_disasm_const_instr(port, "OP_SET_GLOBAL", chunk, offset);
  case OP_READ_LOCAL:
    return mesche_disasm_byte_instr(port, "OP_READ_LOCAL", chunk, offset);
  case OP_SET_LOCAL:
    return mesche_disasm_byte_instr(port, "OP_SET_LOCAL", chunk, offset);
  case OP_JUMP:
    return mesche_disasm_jump_instr(port, "OP_JUMP", 1, chunk, offset);
  case OP_JUMP_IF_FALSE:
    return mesche_disasm_jump_instr(port, "OP_JUMP_IF_FALSE", 1, chunk, offset);
  case OP_CALL:
    return mesche_disasm_bytes_instr(port, "OP_CALL", chunk, offset);
  case OP_TAIL_CALL:
    return mesche_disasm_bytes_instr(port, "OP_TAIL_CALL", chunk, offset);
  case OP_CLOSURE: {
    offset++;
    uint8_t constant = chunk->code[offset++];
    fprintf(port->data.file.fp, "%-16s  %4d  ", "OP_CLOSURE", constant);
    mesche_value_print(port, chunk->constants.values[constant]);
    fprintf(port->data.file.fp, "\n");

    // Disassemble the local and upvalue references
    ObjectFunction *function = AS_FUNCTION(chunk->constants.values[constant]);
    for (int j = 0; j < function->upvalue_count; j++) {
      int is_local = chunk->code[offset++];
      int index = chunk->code[offset++];
      fprintf(port->data.file.fp, "%04d | %s %d\n", offset - 2, is_local ? "local" : "upvalue",
              index);
    }

    return offset;
  }
  case OP_CLOSE_UPVALUE:
    return mesche_disasm_simple_instr(port, "OP_CLOSE_UPVALUE", offset);
  default:
    fprintf(port->data.file.fp, "Unknown opcode: %d\n", instr);
    return offset + 1;
  }
}

void mesche_disasm_function(MeschePort *port, ObjectFunction *function) {
  fprintf(port->data.file.fp, "== ");
  mesche_object_print(port, OBJECT_VAL(function));
  fputs(" ==\n", port->data.file.fp);

  for (int offset = 0; offset < function->chunk.count; /* Intentionally empty */) {
    offset = mesche_disasm_instr(port, &function->chunk, offset);
  }
}
