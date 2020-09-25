#ifndef CLOX_VALUE_H
#define CLOX_VALUE_H

#include "common.h"
#include "clox.h"

#define SIGN_BIT   ((uint64_t)0x8000000000000000)
#define QNAN       ((uint64_t)0x7ffc000000000000)
#define TAG_NIL    1
#define TAG_FALSE  2
#define TAG_TRUE   3

typedef uint64_t Value;

#define NIL_VAL      ((Value)(uint64_t)(QNAN | TAG_NIL))
#define BOOL_VAL(b)  ((b) ? TRUE_VAL : FALSE_VAL)
#define FALSE_VAL    ((Value)(uint64_t)(QNAN | TAG_FALSE))
#define TRUE_VAL     ((Value)(uint64_t)(QNAN | TAG_TRUE))
#define OBJ_VAL(obj) ((Value)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(obj)))

#define IS_NIL(v)    ((v) == NIL_VAL)
#define IS_BOOL(v)   (((v) & FALSE_VAL) == FALSE_VAL)
#define IS_FALSE(v)  ((v) == FALSE_VAL)
#define IS_NUMBER(v) (((v) & QNAN) != QNAN)
#define IS_OBJ(v)    (((v) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

#define NUM_VAL(num)         numToValue(num)
#define AS_BOOL(v)           ((v) == TRUE_VAL)
#define AS_NUM(v)            valueToNum(v)
#define AS_OBJ(v)            ((Obj*)(uintptr_t)((v) & ~(SIGN_BIT | QNAN)))
#define AS_FN(v)             ((ObjFn*)AS_OBJ(v))
#define AS_STRING(v)         ((ObjString*)AS_OBJ(v))
#define AS_CSTRING(v)        (((ObjString*)AS_OBJ(v))->value)
#define AS_NATIVE(v)         (((ObjNative*)AS_OBJ(v)))->fn
#define AS_CLOSURE(v)        ((ObjClosure*)AS_OBJ(v))

#define OBJ_TYPE(value)      (AS_OBJ(value)->type)

typedef union {
  uint64_t bits;
  double num;
} DoubleUnion;

static inline Value numToValue(double num) {
  DoubleUnion data;
  data.num = num;
  return data.bits;
}

static inline double valueToNum(Value v) {
  DoubleUnion data;
  data.bits = v;
  return data.num;
}

typedef struct {
  int capacity;
  int count;
  Value *values;
} ValueArray;

void initValueArray(ValueArray *array);
void writeValueArray(VM *vm, ValueArray *array, Value value);
void freeValueArray(VM *vm, ValueArray *array);

void printValue(Value value);
bool valueEqual(Value a, Value b);

#endif