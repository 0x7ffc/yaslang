#ifndef CLOX_VM_H
#define CLOX_VM_H

#include "common.h"
#include "chunk.h"

#define STACK_MAX 256

typedef struct {
  Chunk *chunk;
  uint8_t *ip;
  Value stack[STACK_MAX];
  Value *sp;   // points to where the next value to be pushed will go
} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

void initVM();

void freeVM();

InterpretResult interpret(const char *source);
void push(Value value);
Value pop();

#endif
