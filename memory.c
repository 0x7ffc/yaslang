#include <stdlib.h>

#include "memory.h"
#include "value.h"
#include "vm.h"

void *reallocate(void *array, size_t old, size_t new) {
  if (new == 0) {
	free(array);
	return NULL;
  }
  // array - Pointer to a memory block previously allocated with
  // malloc, calloc or realloc. Alternatively, this can be a null
  // pointer, in which case a new block is allocated (as if malloc was called).
  return realloc(array, new);
}

static void freeObject(Obj *obj) {
  switch (obj->type) {
	case OBJ_STRING: {
	  FREE(obj);
	  break;
	}
	case OBJ_FN: {
	  freeChunk(&((ObjFn *)obj)->chunk);
	  FREE(obj);
	  break;
	}
  }
}

void freeObjects(VM *vm) {
  Obj *obj = vm->first;
  while (obj != NULL) {
	Obj *next = obj->next;
	freeObject(obj);
	obj = next;
  }
}
