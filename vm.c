#include <stdio.h>

#include "vm.h"
#include "debug.h"
#include "memory.h"

static void resetStack(VM *vm) {
  vm->sp = vm->stack;
  vm->frameCount = 0;
}

void initVM(VM *vm) {
  resetStack(vm);
  vm->first = NULL;
  initTable(&vm->globals);
  initTable(&vm->strings);
}

void freeVM(VM *vm) {
  freeTable(&vm->globals);
  freeTable(&vm->strings);
  freeObjects(vm);
}

static void printStack(VM *vm) {
  printf("         ");
  for (Value* slot = vm->stack; slot < vm->sp; slot++) {
    printf("[ ");
    printValue(*slot);
    printf(" ]");
  }
  printf("\n");
}

static bool call(VM *vm, ObjFn* fn, int argCount) {
  if (argCount != fn->arity) {
    return false;
  }
  if (vm->frameCount == FRAME_MAX) {
    return false;
  }
  CallFrame *frame = &vm->frames[vm->frameCount++];
  frame->fn = fn;
  frame->ip = fn->chunk.code;
  frame->slots = vm->sp - argCount - 1;
  return true;
}

static bool callValue(VM *vm, Value callee, int argCount) {
  if (IS_OBJ(callee)) {
	switch (OBJ_TYPE(callee)) {
	  case OBJ_FN:
	    return call(vm, AS_FN(callee), argCount);
	  default:
	    break;
	}
  }
  return false;
}

static InterpretResult run(VM *vm) {
  CallFrame* frame = &vm->frames[vm->frameCount - 1];
  register uint8_t* ip = frame->ip;

  #define READ_BYTE()     (*ip++)
  #define READ_CONSTANT() (frame->fn->chunk.constants.values[READ_BYTE()])
  #define READ_SHORT()    (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))
  #define READ_STRING()   (AS_STRING(READ_CONSTANT()))
  #define push(value)     (*vm->sp++ = value)
  #define pop()           (*(--vm->sp))
  #define peek()          (*(vm->sp - 1))
  #define peekN(n)        (*(vm->sp - 1 - n))
  #define BINARY_OP(op)             \
  	do                              \
  	{                               \
  		double b = AS_NUM(pop());   \
  		double a = AS_NUM(pop());   \
  		push(NUM_VAL(a op b));      \
    }                               \
    while (false)

  #if DEBUG_TRACE
  	#define debug_trace()                                                   \
  	    do                                                                  \
  		{                                                                   \
  		  printStack(vm);                                                     \
  		  disassembleInstruction(&frame->fn->chunk, (int)(ip - frame->fn->chunk.code));  \
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
  #define CASE(name)  op_##name
  #define DISPATCH()                                            \
      do                                                        \
      {                                                         \
      	debug_trace();                                          \
        goto *dispatchTable[READ_BYTE()];                       \
      }                                                         \
      while (false)

  INTERPRET_LOOP
  {
    CASE(CONSTANT): {
      Value constant = READ_CONSTANT();
      push(constant);
      DISPATCH();
    }
    CASE(CONSTANT_LONG): {
      //TODO
      DISPATCH();
    }
    CASE(ADD): {
      BINARY_OP(+);
      DISPATCH();
    }
    CASE(SUBTRACT): {
      BINARY_OP(-);
      DISPATCH();
    }
    CASE(NEGATE): {
      push(NUM_VAL(-(AS_NUM(pop()))));
      DISPATCH();
    }
    CASE(MULTIPLY): {
      BINARY_OP(*);
      DISPATCH();
    }
    CASE(DIVIDE): {
      BINARY_OP(/);
      DISPATCH();
    }
    CASE(NIL): {
      push(NIL_VAL);
      DISPATCH();
    }
    CASE(TRUE): {
      push(TRUE_VAL);
      DISPATCH();
    }
    CASE(FALSE): {
      push(FALSE_VAL);
      DISPATCH();
    }
    CASE(PRINT): {
      printValue(pop());
      printf("\n");
      DISPATCH();
    }
    CASE(POP): {
      pop();
      DISPATCH();
    }
    CASE(DEFINE_GLOBAL): {
      ObjString *name = READ_STRING();
      tableSet(&vm->globals, name, peek());
      DISPATCH();
    }
    CASE(GET_GLOBAL): {
      ObjString *name = READ_STRING();
      Value value;
      if (!tableGet(&vm->globals, name, &value))
        return INTERPRET_RUNTIME_ERROR;
      push(value);
      DISPATCH();
    }
    CASE(SET_GLOBAL): {
      ObjString *name = READ_STRING();
      if (tableSet(&vm->globals, name, peek())) {
        // Is a new global.
        tableDelete(&vm->globals, name);
        return INTERPRET_RUNTIME_ERROR;
      }
      DISPATCH();
    }
    CASE(GET_LOCAL): {
      int8_t slot = READ_BYTE();
      push(frame->slots[slot]);
      DISPATCH();
    }
    CASE(SET_LOCAL): {
      uint8_t slot = READ_BYTE();
      frame->slots[slot] = peek();
      DISPATCH();
    }
    CASE(JUMP_IF): {
      uint16_t offset = READ_SHORT();
      Value condition = pop();
      if (IS_FALSE(condition) || IS_NIL(condition))
        ip += offset;
      DISPATCH();
    }
    CASE(JUMP): {
      uint16_t offset = READ_SHORT();
      ip += offset;
      DISPATCH();
    }
    CASE(LOOP): {
      uint16_t offset = READ_SHORT();
      ip -= offset;
      DISPATCH();
    }
    CASE(CALL): {
      int argCount = READ_BYTE();
      frame->ip = ip;
      if (!callValue(vm, peekN(argCount), argCount)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      frame = &vm->frames[vm->frameCount - 1];
      ip = frame->ip;
      DISPATCH();
    }
    CASE(RETURN): {
      Value result = pop();
      vm->frameCount--;
      if (vm->frameCount == 0) {
        pop();
        return INTERPRET_OK;
      }
      vm->sp = frame->slots;
      push(result);
      frame = &vm->frames[vm->frameCount - 1];
      ip = frame->ip;
      DISPATCH();
    }
  }

  #undef READ_BYTE
  #undef READ_CONSTANT
  #undef BINARY_OP
  #undef READ_SHORT
}

InterpretResult interpret(VM *vm, const char *source) {
  ObjFn* fn = compile(vm, source);
  callValue(vm, OBJ_VAL(fn), 0);
  return run(vm);
}