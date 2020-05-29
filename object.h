#ifndef CLOX_OBJECT_H
#define CLOX_OBJECT_H

#include "common.h"
#include "chunk.h"
#include "clox.h"

typedef enum {
  OBJ_STRING,
  OBJ_FN,
} ObjType;

typedef struct sObj Obj;;
typedef struct sObjString ObjString;

struct sObj {
  ObjType type;
  struct sObj *next;
};

typedef struct {
  Obj obj;
  int arity;
  Chunk chunk;
  ObjString *name;
} ObjFn;

ObjFn *newFn(VM *vm);

struct sObjString {
  Obj obj;
  uint32_t length;
  uint32_t hash;
  char value[];
};

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
void freeTable(Table *table);
bool tableGet(Table *table, ObjString *key, Value *value);
bool tableSet(Table *table, ObjString *key, Value value);
bool tableDelete(Table *table, ObjString *key);
void tableAddAll(Table *from, Table *to);
ObjString *tableFindString(Table *table, const char *chars, int length,
						   uint32_t hash);
void printObject(Value value);

#endif
