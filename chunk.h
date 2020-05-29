#ifndef CLOX_CHUNK_H
#define CLOX_CHUNK_H

#include "common.h"
#include "value.h"

typedef enum {
#define OPCODE(name, _) OP_##name,
#include "opcode.h"
#undef OPCODE
} OpCode;

typedef struct Chunk {
  int count;       // in use
  int capacity;    // allocated
  uint8_t *code;
  ValueArray constants;
} Chunk;

void initChunk(Chunk *chunk);

void freeChunk(Chunk *chunk);

void writeChunk(Chunk *chunk, uint8_t byte);

int addConstant(Chunk *chunk, Value value);

#endif
