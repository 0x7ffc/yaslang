#ifndef CLOX_MEMORY_H
#define CLOX_MEMORY_H

#include "common.h"

#define GROW_CAPACITY(capacity) \
    ((capacity) == 0 ? 8 : (capacity * 2))    // 8 as default ???

#define GROW_ARRAY(array, type, old, new) \
    (type*)reallocate(array, sizeof(type) * (old), sizeof(type) * (new))

#define FREE_ARRAY(array) \
    reallocate(array, 0, 0);


// oldSize 	    newSize 	            Operation
// 0 	        Non‑zero 	              Allocate new block.
// Non‑zero 	0 	                    Free allocation.
// Non‑zero 	Smaller than oldSize 	  Shrink existing allocation.
// Non‑zero 	Larger than oldSize 	  Grow existing allocation.
//
// As an implication, size_t is a type guaranteed to hold any array index.

void *reallocate(void *array, size_t old, size_t new);

#endif
