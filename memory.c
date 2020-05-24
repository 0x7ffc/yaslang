#include <stdlib.h>
#include "memory.h"

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
