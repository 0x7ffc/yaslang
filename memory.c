#include <stdlib.h>

#include "memory.h"
#include "value.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 2

void *reallocate(VM *vm, void *array, size_t old, size_t new) {
  vm->bytesAllocated += new - old;
#if DEBUG_STRESS_GC
  if (new > 0) { gc(vm); }
#else
  if (new > 0 && vm->bytesAllocated > vm->nextGC) gc(vm);
#endif
  if (new == 0) {
	free(array);
	return NULL;
  }
  // array - Pointer to a memory block previously allocated with
  // malloc, calloc or realloc. Alternatively, this can be a null
  // pointer, in which case a new block is allocated (as if malloc was called).
  return realloc(array, new);
}

static void freeObject(VM *vm, Obj *obj) {
  #ifdef DEBUG_LOG_GC
  printf("%p free type %d\n", (void*)obj, obj->type);
  #endif
  switch (obj->type) {
	case OBJ_CLOSURE:
	case OBJ_NATIVE:
	case OBJ_UPVALUE:
	case OBJ_STRING: {
	  DEALLOCATE(vm, obj);
	  break;
	}
	case OBJ_FN: {
	  freeChunk(vm, &((ObjFn *)obj)->chunk);
	  DEALLOCATE(vm, obj);
	  break;
	}
  }
}

void freeObjects(VM *vm) {
  Obj *obj = vm->first;
  while (obj != NULL) {
	Obj *next = obj->next;
	freeObject(vm, obj);
	obj = next;
  }
  free(vm->grayStack);
}

void markObject(VM *vm, Obj* object) {
  if (object == NULL) return;
  if (object->isMarked) return;
  #ifdef DEBUG_LOG_GC
  printf("%p mark ", (void*)object);
  printValue(OBJ_VAL(object));
  printf("\n");
  #endif
  object->isMarked = true;

  if (vm->grayCapacity < vm->grayCount + 1) {
    vm->grayCapacity = GROW_CAPACITY(vm->grayCapacity);
    vm->grayStack = realloc(vm->grayStack, sizeof(Obj*) * vm->grayCapacity);
  }

  vm->grayStack[vm->grayCount++] = object;

}

void markValue(VM *vm, Value value) {
  if (!IS_OBJ(value)) return;
  markObject(vm, AS_OBJ(value));
}

static void markRoots(VM *vm) {
  for (Value* slot = vm->stack; slot < vm->sp; slot++) {
	markValue(vm, *slot);
  }
  for (int i = 0; i < vm->frameCount; i++) {
	markObject(vm, (Obj*)vm->frames[i].closure);
  }
  for (ObjUpvalue* upvalue = vm->openUpvalues;
	   upvalue != NULL;
	   upvalue = upvalue->next) {
	markObject(vm, (Obj*)upvalue);
  }
  markTable(vm, &vm->globals);
  markCompilerRoots(vm, vm->compiler);
}

static void markArray(VM *vm, ValueArray* array) {
  for (int i = 0; i < array->count; i++) {
    markValue(vm, array->values[i]);
  }
}

static void blackenObject(VM *vm, Obj* obj) {
  #ifdef DEBUG_LOG_GC
  printf("%p blacken ", (void*)obj);
  printValue(OBJ_VAL(obj));
  printf("\n");
  #endif
  switch (obj->type) {
    case OBJ_CLOSURE: {
      ObjClosure *closure = (ObjClosure*)obj;
      markObject(vm, (Obj*)closure->fn);
      for (int i = 0; i < closure->upvalueCount; i++) {
        markObject(vm, (Obj*)closure->upvalues[i]);
      }
      break;
    }
    case OBJ_FN: {
      ObjFn* fn = (ObjFn*)obj;
      markObject(vm, (Obj*)fn->name);
      markArray(vm, &fn->chunk.constants);
	}
    case OBJ_UPVALUE: {
	  markValue(vm, ((ObjUpvalue *)obj)->closed);
	  break;
	}
    case OBJ_NATIVE:
    case OBJ_STRING:
      break;
  }
}

static void traceReferences(VM *vm) {
  while (vm->grayCount > 0) {
    Obj* obj = vm->grayStack[--vm->grayCount];
    blackenObject(vm, obj);
  }
}

static void sweep(VM *vm) {
  Obj* previous = NULL;
  Obj* obj = vm->first;
  while (obj != NULL) {
    if (obj->isMarked) {
      obj->isMarked = false;
      previous = obj;
      obj = obj->next;
    } else {
      Obj* unreached = obj;
      obj = obj->next;
      if (previous != NULL) {
        previous->next = obj;
      } else {
        vm->first = obj;
      }
      freeObject(vm, unreached);
    }
  }
}

void gc(VM *vm) {
  #ifdef DEBUG_LOG_GC
  printf("-- gc begin\n");
  size_t before = vm->bytesAllocated;
  #endif

  markRoots(vm);
  traceReferences(vm);
  tableRemoveWhite(&vm->strings);
  sweep(vm);

  vm->nextGC = vm->bytesAllocated * GC_HEAP_GROW_FACTOR;

  #ifdef DEBUG_LOG_GC
  printf("-- gc end\n");
  printf("   collected %ld bytes (from %ld to %ld) next at %ld\n",
		 before - vm->bytesAllocated, before, vm->bytesAllocated,
		 vm->nextGC);
  #endif
}
