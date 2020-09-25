#include <stdio.h>
#include <string.h>
#include <time.h>

#include "vm.h"
#include "debug.h"
#include "memory.h"

static void defineNative(VM *vm, const char* name, NativeFn fn) {
  *vm->sp++ = OBJ_VAL(newStringLength(vm, name, (int)strlen(name)));
  *vm->sp++ = OBJ_VAL(newNative(vm, fn));
  tableSet(vm, &vm->globals, AS_STRING(vm->stack[0]), vm->stack[1]);
  vm->sp -= 2;
}

static Value clockNative(int argCount, Value* args) {
  return NUM_VAL((double)clock() / CLOCKS_PER_SEC);
}

static void resetStack(VM *vm) {
  vm->sp = vm->stack;
  vm->frameCount = 0;
  vm->openUpvalues = NULL;
}

void initVM(VM *vm) {
  resetStack(vm);
  vm->first = NULL;
  vm->bytesAllocated = 0;
  vm->nextGC = 1024 * 1024;
  vm->compiler = NULL;
  vm->grayCount = 0;
  vm->grayCapacity = 0;
  vm->grayStack = NULL;
  initTable(&vm->globals);
  initTable(&vm->strings);
  defineNative(vm, "clock", clockNative);
}

void freeVM(VM *vm) {
  freeTable(vm, &vm->globals);
  freeTable(vm, &vm->strings);
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

static bool call(VM *vm, ObjClosure * closure, int argCount) {
  if (argCount != closure->fn->arity) {
    return false;
  }
  if (vm->frameCount == FRAME_MAX) {
    return false;
  }
  CallFrame *frame = &vm->frames[vm->frameCount++];
  frame->closure = closure;
  frame->ip = closure->fn->chunk.code;
  frame->slots = vm->sp - argCount - 1;
  return true;
}

static bool callValue(VM *vm, Value callee, int argCount) {
  if (IS_OBJ(callee)) {
	switch (OBJ_TYPE(callee)) {
	  case OBJ_CLOSURE:
	    return call(vm, AS_CLOSURE(callee), argCount);
	  case OBJ_NATIVE: {
	    NativeFn native = AS_NATIVE(callee);
	    Value result = native(argCount, vm->sp - argCount);
	    vm->sp -= argCount + 1;
	    *vm->sp++ = result;
	    return true;
	  }
	  default:
	    break;
	}
  }
  return false;
}

static ObjUpvalue *captureUpvalue(VM *vm, Value *local) {
  ObjUpvalue *prevUpvalue = NULL;
  ObjUpvalue *upvalue = vm->openUpvalues;
  while (upvalue != NULL && upvalue->location > local) {
    prevUpvalue = upvalue;
    upvalue = upvalue->next;
  }

  if (upvalue != NULL && upvalue->location == local) return upvalue;
  ObjUpvalue *createdUpvalue = newUpvalue(vm, local);
  createdUpvalue->next = upvalue;
  if (prevUpvalue == NULL) {
    vm->openUpvalues = createdUpvalue;
  } else {
    prevUpvalue->next = createdUpvalue;
  }

  return createdUpvalue;
}

static void closeUpvalues(VM *vm, const Value* last) {
  while (vm->openUpvalues != NULL &&
	  vm->openUpvalues->location >= last) {
	ObjUpvalue* upvalue = vm->openUpvalues;
	upvalue->closed = *upvalue->location;
	upvalue->location = &upvalue->closed;
	vm->openUpvalues = upvalue->next;
  }
}

static InterpretResult run(VM *vm) {
  CallFrame* frame = &vm->frames[vm->frameCount - 1];
  register uint8_t* ip = frame->ip;

  #define READ_BYTE()     (*ip++)
  #define READ_CONSTANT() (frame->closure->fn->chunk.constants.values[READ_BYTE()])
  #define READ_SHORT()    (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))
  #define READ_STRING()   (AS_STRING(READ_CONSTANT()))
  #define push(value)     (*vm->sp++ = value)
  #define pop()           (*(--vm->sp))
  #define peek()          (*(vm->sp - 1))
  #define peekN(n)        (*(vm->sp - 1 - (n)))
  #define BINARY_OP(type, op)             \
  	do                              \
  	{                               \
  		double b = AS_NUM(pop());   \
  		double a = AS_NUM(pop());   \
  		push(type(a op b));      \
    }                               \
    while (false)

  #if DEBUG_TRACE
  	#define debug_trace()                                                   \
  	    do                                                                  \
  		{                                                                   \
  		  printStack(vm);                                                     \
  		  disassembleInstruction(&frame->closure->fn->chunk,  \
                                 (int)(ip - frame->closure->fn->chunk.code));  \
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
      BINARY_OP(NUM_VAL, +);
      DISPATCH();
    }
    CASE(SUBTRACT): {
      BINARY_OP(NUM_VAL, -);
      DISPATCH();
    }
    CASE(NEGATE): {
      push(NUM_VAL(-(AS_NUM(pop()))));
      DISPATCH();
    }
    CASE(MULTIPLY): {
      BINARY_OP(NUM_VAL, *);
      DISPATCH();
    }
    CASE(DIVIDE): {
      BINARY_OP(NUM_VAL, /);
      DISPATCH();
    }
    CASE(LESS): {
      BINARY_OP(BOOL_VAL, <);
      DISPATCH();
    }
    CASE(EQ): {
      Value b = pop();
      Value a = pop();
      push(BOOL_VAL(valueEqual(a, b)));
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
      tableSet(vm, &vm->globals, name, peek());
      pop();
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
      if (tableSet(vm, &vm->globals, name, peek())) {
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
    CASE(GET_UPVALUE): {
      uint8_t slot = READ_BYTE();
      push(*frame->closure->upvalues[slot]->location);
      DISPATCH();
    }
    CASE(SET_UPVALUE): {
      uint8_t slot = READ_BYTE();
      *frame->closure->upvalues[slot]->location = peek();
      DISPATCH();
    }
    CASE(CLOSE_UPVALUE): {
      closeUpvalues(vm, vm->sp - 1);
      pop();
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
    CASE(CLOSURE): {
      ObjFn* inner = AS_FN(READ_CONSTANT());
      ObjClosure *closure = newClosure(vm, inner);
      push(OBJ_VAL(closure));
      for (int i = 0; i < closure->upvalueCount; i++) {
        uint8_t isLocal = READ_BYTE();
        uint8_t index = READ_BYTE();
        if (isLocal) {
          closure->upvalues[i] = captureUpvalue(vm, frame->slots + index);
        } else {
          closure->upvalues[i] = frame->closure->upvalues[index];
        }
      }
      DISPATCH();
    }
    CASE(RETURN): {
      Value result = pop();
      closeUpvalues(vm, frame->slots);
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
    CASE(TAIL_CALL): {
      int argCount = READ_BYTE();
      ObjClosure *closure = AS_CLOSURE(peekN(argCount));
      frame->closure = closure;
      frame->ip = closure->fn->chunk.code;
      for (int i = 0; i <= argCount; i++) {
        frame->slots[i] = peekN(argCount - i);
      }
      vm->sp = frame->slots + argCount + 1;
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
  push(OBJ_VAL(fn));
  ObjClosure* closure = newClosure(vm, fn);
  pop();
  push(OBJ_VAL(closure));
  callValue(vm, OBJ_VAL(closure), 0);
  return run(vm);
}