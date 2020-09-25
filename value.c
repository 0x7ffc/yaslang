#include <stdio.h>
#include <string.h>

#include "vm.h"
#include "memory.h"

void initValueArray(ValueArray *array) {
  array->values = NULL;
  array->capacity = 0;
  array->count = 0;
}

void writeValueArray(VM *vm, ValueArray *array, Value value) {
  if (array->count == array->capacity) {
	array->capacity = GROW_CAPACITY(array->capacity);
	array->values = GROW_ARRAY(vm, array->values, Value,
							   array->count, array->capacity);
  }
  array->values[array->count] = value;
  array->count++;
}

void freeValueArray(VM *vm, ValueArray *array) {
  DEALLOCATE(vm, array->values);
  initValueArray(array);
}

void printValue(Value value) {
  if (IS_BOOL(value)) {
	printf(AS_BOOL(value) ? "true" : "false");
  } else if (IS_NIL(value)) {
	printf("nil");
  } else if (IS_NUMBER(value)) {
	printf("%g", AS_NUM(value));
  } else if (IS_OBJ(value)) {
	printObject(value);
  }
}

bool valueEqual(Value a, Value b) {
  if (IS_NUMBER(a) && IS_NUMBER(b)) return AS_NUM(a) == AS_NUM(b);
  return a == b;
}