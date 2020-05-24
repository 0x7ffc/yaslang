#include <stdio.h>

#include "vm.h"
#include "debug.h"
#include "compiler.h"

VM vm;

static void resetStack() {
  vm.sp = vm.stack;
}

void initVM() {
  resetStack();
}

void freeVM() {

}

void push(Value value) {
  *vm.sp = value;
  vm.sp++;
}

Value pop() {
  vm.sp--;
  return *vm.sp;
}

static void printStack() {
  printf("         ");
  for (Value* slot = vm.stack; slot < vm.sp; slot++) {
    printf("[ ");
    printValue(*slot);
    printf(" ]");
  }
  printf("\n");
}

static InterpretResult run() {
  #define READ_BYTE() (*vm.ip++)
  #define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
  #define BINARY_OP(op)     \
  	do                      \
  	{                       \
  		double b = pop();   \
  		double a = pop();   \
  		push(a op b);       \
    }                       \
    while (false)

  #if DEBUG_TRACE
  	#define debug_trace()                                                   \
  	    do                                                                  \
  		{                                                                   \
  		  printStack();                                                     \
  		  disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));  \
        }                                                                   \
        while (false)
  #else
    #define debug_trace() do { } while (false)
  #endif
  static void *dispatchTable[] = {
	  #define OPCODE(name, _) &&op_##name,
	  #include "opcode.h"
	  #undef OPCODE
  };
  #define INTERPRET_LOOP DISPATCH();
  #define CASE_OP(name)  op_##name
  #define DISPATCH()                                            \
      do                                                        \
      {                                                         \
      	debug_trace();                                          \
        goto *dispatchTable[instruction = (OpCode)READ_BYTE()]; \
      }                                                         \
      while (false)

  OpCode instruction;
  INTERPRET_LOOP
  {
    CASE_OP(CONSTANT): {
      Value constant = READ_CONSTANT();
      push(constant);
      DISPATCH();
    }
    CASE_OP(CONSTANT_LONG): {
      //TODO
      DISPATCH();
    }
    CASE_OP(ADD): {
      BINARY_OP(+);
      DISPATCH();
    }
    CASE_OP(SUBTRACT): {
      BINARY_OP(-);
      DISPATCH();
    }
    CASE_OP(NEGATE): {
      push(-pop());
      DISPATCH();
    }
    CASE_OP(MULTIPLY): {
      BINARY_OP(*);
      DISPATCH();
    }
    CASE_OP(DIVIDE): {
      BINARY_OP(/);
      DISPATCH();
    }
    CASE_OP(RETURN): {
      printValue(pop());
      printf("\n");
      return INTERPRET_OK;
    }
  }

  #undef READ_BYTE
  #undef READ_CONSTANT
  #undef BINARY_OP
}

InterpretResult interpret(const char *source) {
  Chunk chunk;
  initChunk(&chunk);
  if (!compile(source, &chunk)) {
	freeChunk(&chunk);
	return INTERPRET_COMPILE_ERROR;
  }
  vm.chunk = &chunk;
  vm.ip = vm.chunk->code;
  InterpretResult result = run();
  freeChunk(&chunk);
  return result;
}