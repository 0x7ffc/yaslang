#include <stdio.h>

#include "value.h"
#include "memory.h"

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
  FREE_ARRAY(array->values);
  initValueArray(array);
}

void printValue(Value value) {
  printf("%g", value);
}