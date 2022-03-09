#ifndef mesche_chunk_h
#define mesche_chunk_h

#include "mem.h"
#include "value.h"
#include <stdint.h>

typedef struct {
  int capacity;
  int count;
  uint8_t *code;
  int *lines;
  ValueArray constants;
} Chunk;

void mesche_chunk_init(Chunk *chunk);
void mesche_chunk_write(MescheMemory *mem, Chunk *chunk, uint8_t byte, int line);
void mesche_chunk_insert_space(MescheMemory *mem, Chunk *chunk, int start_offset, int space_size);
int mesche_chunk_constant_add(MescheMemory *mem, Chunk *chunk, Value value);
void mesche_chunk_free(MescheMemory *mem, Chunk *chunk);

#endif
