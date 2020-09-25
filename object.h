#ifndef CLOX_OBJECT_H
#define CLOX_OBJECT_H

#include "common.h"
#include "chunk.h"
#include "clox.h"


// 4 Bytes.
typedef enum {
  OBJ_STRING,
  OBJ_NATIVE,
  OBJ_FN,
  OBJ_CLOSURE,
  OBJ_UPVALUE,
} ObjType;

typedef struct sObj Obj;;
typedef struct sObjString ObjString;

// 16 Bytes.
struct sObj {
  ObjType type;
  bool isMarked;
  struct sObj *next;
};

typedef struct {
  Obj obj;
  int arity;
  int upvalueCount;
  Chunk chunk;
  ObjString *name;
} ObjFn;

ObjFn *newFn(VM *vm);

typedef Value (*NativeFn)(int argCount, Value* args);

typedef struct {
  Obj obj;
  NativeFn fn;
} ObjNative;

ObjNative *newNative(VM *vm, NativeFn fn);

struct sObjString {
  Obj obj;
  uint32_t length;
  uint32_t hash;
  char value[];
};

typedef struct sUpvalue {
  Obj obj;
  Value *location;
  Value closed;
  struct sUpvalue *next;
} ObjUpvalue;

ObjUpvalue *newUpvalue(VM *vm, Value* slot);

typedef struct {
  Obj obj;
  ObjFn *fn;
  int upvalueCount;
  ObjUpvalue *upvalues[];
} ObjClosure;

ObjClosure *newClosure(VM *vm, ObjFn* fn);

static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

Value newStringLength(VM *vm, const char *text, size_t length);

typedef struct {
  ObjString *key;
  Value value;
} Entry;

typedef struct {
  int count;
  int capacity;
  Entry *entries;
} Table;

#define TABLE_MAX_LOAD 0.75

void initTable(Table *table);
void freeTable(VM *vm, Table *table);
bool tableGet(Table *table, ObjString *key, Value *value);
bool tableSet(VM *vm, Table *table, ObjString *key, Value value);
bool tableDelete(Table *table, ObjString *key);
ObjString *tableFindString(Table *table, const char *chars, int length,
						   uint32_t hash);
void markTable(VM *vm, Table* table);
void tableRemoveWhite(Table* table);

void printObject(Value value);

#endif
