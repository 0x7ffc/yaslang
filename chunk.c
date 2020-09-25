#include <stdlib.h>

#include "chunk.h"
#include "memory.h"

void initChunk(Chunk *chunk) {
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = NULL;
  initValueArray(&chunk->constants);
}

void freeChunk(VM *vm, Chunk *chunk) {
  DEALLOCATE(vm, chunk->code);
  freeValueArray(vm, &chunk->constants);
  initChunk(chunk);
}

void writeChunk(VM *vm, Chunk *chunk, uint8_t byte) {
  if (chunk->count == chunk->capacity) {
	chunk->capacity = GROW_CAPACITY(chunk->capacity);
	chunk->code = GROW_ARRAY(vm, chunk->code, uint8_t,
							 chunk->count, chunk->capacity);
  }
  chunk->code[chunk->count] = byte;
  chunk->count++;
}

int addConstant(VM *vm, Chunk *chunk, Value value) {
  writeValueArray(vm, &chunk->constants, value);
  return chunk->constants.count - 1;
}

