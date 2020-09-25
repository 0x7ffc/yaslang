#ifndef CLOX_CHUNK_H
#define CLOX_CHUNK_H

#include "common.h"
#include "value.h"
#include "clox.h"

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

void freeChunk(VM *vm, Chunk *chunk);

void writeChunk(VM *vm, Chunk *chunk, uint8_t byte);

int addConstant(VM *vm, Chunk *chunk, Value value);


#define ARRAY_NEW(arr) \
  do { \
    size_t *raw = malloc(2 * sizeof(size_t)); \
    raw[0] = 0; \
    raw[1] = 0; \
    arr = (void*)&raw[2]; \
  } while (false)

#define ARRAY_SIZE(arr) *((size_t*)(arr) - 2)

#define ARRAY_FREE(arr) \
  do { \
    size_t *raw = ((size_t*)(arr) - 2); \
    free(raw); \
    arr = NULL; \
  } while (false)

#define ARRAY_PUSH(arr, v) \
  do { \
    size_t *raw = ((size_t*)(arr) - 2); \
    if (raw[0] + 1 > raw[1]) { \
      raw[1] = (raw[1] == 0) ? 8 : raw[1] * 2; \
      raw = realloc(raw, 2 * sizeof(size_t) + raw[1] * sizeof(v)); \
      arr = (void*)&raw[2]; \
    } \
    arr[raw[0]] = (v); \
    raw[0] += 1; \
  } while (false)

#endif
