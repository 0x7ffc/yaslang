#ifndef CLOX_MEMORY_H
#define CLOX_MEMORY_H

#include "common.h"
#include "clox.h"

#define GROW_CAPACITY(capacity) \
    ((capacity) == 0 ? 8 : (capacity * 2))    // 8 as default ???

#define ALLOCATE_FLEX(main, sub, count) \
    (main*)reallocate(NULL, 0, sizeof(main) + sizeof(sub) * count)

#define ALLOCATE(type, count) \
    (type*)reallocate(NULL, 0, sizeof(type) * (count))

#define GROW_ARRAY(array, type, old, new) \
    (type*)reallocate(array, sizeof(type) * (old), sizeof(type) * (new))

#define FREE(pointer) \
    reallocate(pointer, 0, 0)


// oldSize 	    newSize 	            Operation
// 0 	        Non‑zero 	              Allocate new block.
// Non‑zero 	0 	                    Free allocation.
// Non‑zero 	Smaller than oldSize 	  Shrink existing allocation.
// Non‑zero 	Larger than oldSize 	  Grow existing allocation.
//
// As an implication, size_t is a type guaranteed to hold any array index.

void *reallocate(void *array, size_t old, size_t new);
void freeObjects(VM *vm);

#endif
