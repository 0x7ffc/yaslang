#include <stdio.h>
#include <string.h>

#include "vm.h"
#include "memory.h"
#include "object.h"

void initValueArray(ValueArray *array) {
  array->values = NULL;
  array->capacity = 0;
  array->count = 0;
}

void writeValueArray(ValueArray *array, Value value) {
  if (array->count == array->capacity) {
	array->capacity = GROW_CAPACITY(array->capacity);
	array->values = GROW_ARRAY(array->values, Value,
							   array->count, array->capacity);
  }
  array->values[array->count] = value;
  array->count++;
}

void freeValueArray(ValueArray *array) {
  FREE(array->values);
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