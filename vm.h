#ifndef CLOX_VM_H
#define CLOX_VM_H

#include "common.h"
#include "value.h"
#include "clox.h"
#include "object.h"
#include "chunk.h"
#include "compiler.h"

typedef struct {
  ObjFn *fn;
  uint8_t *ip;
  Value *slots;
} CallFrame;

#define FRAME_MAX 64
#define STACK_MAX (FRAME_MAX * UINT8_MAX)

struct VM {
  CallFrame frames[STACK_MAX];
  int frameCount;
  //Chunk *chunk;
  //uint8_t *ip;
  Value stack[STACK_MAX];
  Value *sp;   // points to where the next value to be pushed will go
  Table globals;
  Table strings;
  Obj *first;
  Compiler *compiler;
};

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

void initVM(VM *vm);
void freeVM(VM *vm);
InterpretResult interpret(VM *vm, const char *source);

#endif
