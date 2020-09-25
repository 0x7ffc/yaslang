#ifndef CLOX_MEMORY_H
#define CLOX_MEMORY_H

#include "common.h"
#include "clox.h"
#include "value.h"
#include "object.h"

#define GROW_CAPACITY(capacity) \
    ((capacity) == 0 ? 8 : (capacity * 2))    // 8 as default ???

#define ALLOCATE(vm, type) \
	((type*)reallocate(vm, NULL, 0, sizeof(type)))

#define ALLOCATE_FLEX(vm, main, sub, count) \
	((main*)reallocate(vm, NULL, 0, sizeof(main) + sizeof(sub) * count))

#define ALLOCATE_ARRAY(vm, type, count) \
    ((type*)reallocate(vm, NULL, 0, sizeof(type) * (count)))

#define GROW_ARRAY(vm, array, type, old, new) \
	((type*)reallocate(vm, array, sizeof(type) * (old), sizeof(type) * (new)))

#define DEALLOCATE(vm, pointer) \
    reallocate(vm, pointer, 0, 0)


// oldSize 	    newSize 	            Operation
// 0 	        Non‑zero 	              Allocate new block.
// Non‑zero 	0 	                    Free allocation.
// Non‑zero 	Smaller than oldSize 	  Shrink existing allocation.
// Non‑zero 	Larger than oldSize 	  Grow existing allocation.
//
// As an implication, size_t is a type guaranteed to hold any array index.

void *reallocate(VM *vm, void *array, size_t old, size_t new);
void markObject(VM* vm, Obj* object);
void markValue(VM *vm, Value value);
void freeObjects(VM *vm);
void gc(VM *vm);

#endif
