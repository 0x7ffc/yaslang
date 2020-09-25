#include <string.h>
#include <stdio.h>

#include "object.h"
#include "vm.h"
#include "memory.h"

static void initObj(VM *vm, Obj *obj, ObjType type, size_t size) {
  obj->type = type;
  obj->next = vm->first;
  obj->isMarked = false;
  vm->first = obj;
  #ifdef DEBUG_LOG_GC
  printf("%p allocate %ld for %d\n", (void*)obj, size, type);
  #endif
}

ObjFn *newFn(VM *vm) {
  ObjFn *fn = ALLOCATE(vm, ObjFn);
  initObj(vm, &fn->obj, OBJ_FN, sizeof(*fn));
  fn->arity = 0;
  fn->upvalueCount = 0;
  fn->name = NULL;
  initChunk(&fn->chunk);
  return fn;
}

ObjClosure *newClosure(VM *vm, ObjFn* fn) {
  ObjClosure *closure = ALLOCATE_FLEX(vm, ObjClosure, ObjUpvalue*, fn->upvalueCount);
  initObj(vm, &closure->obj, OBJ_CLOSURE, sizeof(*closure));
  for (int i = 0; i < fn->upvalueCount; i++) {
	closure->upvalues[i] = NULL;
  }
  closure->fn = fn;
  closure->upvalueCount = fn->upvalueCount;
  return closure;
}

ObjUpvalue *newUpvalue(VM *vm, Value *slot) {
  ObjUpvalue *upvalue = ALLOCATE(vm, ObjUpvalue);
  initObj(vm, &upvalue->obj, OBJ_UPVALUE, sizeof(*upvalue));
  upvalue->location = slot;
  upvalue->closed = NIL_VAL;
  upvalue->next = NULL;
  return upvalue;
}

ObjNative *newNative(VM *vm, NativeFn fn) {
  ObjNative *native = ALLOCATE(vm, ObjNative);
  initObj(vm, &native->obj, OBJ_NATIVE, sizeof(*native));
  native->fn = fn;
  return native;
}

static ObjString *allocateString(VM *vm, size_t length) {
  ObjString *string = ALLOCATE_FLEX(vm, ObjString, char, length + 1);
  initObj(vm, &string->obj, OBJ_STRING, sizeof(*string));
  string->length = (int)length;
  string->value[length] = '\0';
  return string;
}

static uint32_t hashString(const char *key, size_t length) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
	hash ^= key[i];
	hash *= 16777619;
  }
  return hash;
}

Value newStringLength(VM *vm, const char *text, size_t length) {
  uint32_t hash = hashString(text, length);
  ObjString *interned = tableFindString(&vm->strings, text, length,
										hash);
  if (interned != NULL) return OBJ_VAL(interned);
  ObjString *string = allocateString(vm, length);
  if (length > 0 && text != NULL) memcpy(string->value, text, length);
  string->hash = hash;
  tableSet(vm, &vm->strings, string, NIL_VAL);
  return OBJ_VAL(string);
}

void printObject(Value value) {
  switch (OBJ_TYPE(value)) {
	case OBJ_STRING: {
	  printf("%s", AS_CSTRING(value));
	  break;
	}
	case OBJ_FN: {
	  ObjFn *fn = AS_FN(value);
	  if (fn->name == NULL) {
		printf("<script>");
		return;
	  }
	  printf("<fn %s>", fn->name->value);
	  break;
	}
	case OBJ_NATIVE: {
	  printf("<native fn>");
	  break;
	}
	case OBJ_CLOSURE: {
	  ObjFn *fn = AS_CLOSURE(value)->fn;
	  if (fn->name == NULL) {
		printf("<script>");
		return;
	  }
	  printf("<fn %s>", fn->name->value);
	  break;
	}
	case OBJ_UPVALUE: {
	  printf("upvalue");
	  break;
	}
  }
}

void initTable(Table *table) {
  table->count = 0;
  table->capacity = 0;
  table->entries = NULL;
}

void freeTable(VM *vm, Table *table) {
  DEALLOCATE(vm, table->entries);
  initTable(table);
}

static Entry *findEntry(Entry *entries, int capacity,
						ObjString *key) {
  uint32_t index = key->hash % capacity;
  Entry *tombstone = NULL;
  for (;;) {
	Entry *entry = &entries[index];
	if (entry->key == NULL) {
	  if (IS_NIL(entry->value)) {
		return tombstone != NULL ? tombstone : entry;
	  } else {
		if (tombstone == NULL) tombstone = entry;
	  }
	} else if (entry->key == key) {
	  return entry;
	}
	index = (index + 1) % capacity;
  }
}

static void adjustCapacity(VM *vm, Table *table, int capacity) {
  Entry *entries = ALLOCATE_ARRAY(vm, Entry, capacity);
  for (int i = 0; i < capacity; i++) {
	entries[i].key = NULL;
	entries[i].value = NIL_VAL;
  }
  table->count = 0;
  for (int i = 0; i < table->capacity; i++) {
	Entry *entry = &table->entries[i];
	if (entry->key == NULL) continue;

	Entry *dest = findEntry(entries, capacity, entry->key);
	dest->key = entry->key;
	dest->value = entry->value;
	table->count++;
  }
  DEALLOCATE(vm, table->entries);
  table->entries = entries;
  table->capacity = capacity;
}

bool tableGet(Table *table, ObjString *key, Value *value) {
  if (table->count == 0) return false;

  Entry *entry = findEntry(table->entries, table->capacity, key);
  if (entry->key == NULL) return false;

  *value = entry->value;
  return true;
}

bool tableSet(VM *vm, Table *table, ObjString *key, Value value) {
  if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
	int capacity = GROW_CAPACITY(table->capacity);
	adjustCapacity(vm, table, capacity);
  }
  Entry *entry = findEntry(table->entries, table->capacity, key);
  bool isNewKey = entry->key == NULL;
  if (isNewKey && IS_NIL(entry->value)) table->count++;
  entry->key = key;
  entry->value = value;
  return isNewKey;
}

bool tableDelete(Table *table, ObjString *key) {
  if (table->count == 0) return false;

  // Find the entry.
  Entry *entry = findEntry(table->entries, table->capacity, key);
  if (entry->key == NULL) return false;

  // Place a tombstone in the entry.
  entry->key = NULL;
  entry->value = BOOL_VAL(true);

  return true;
}

ObjString *tableFindString(Table *table, const char *chars, int length,
						   uint32_t hash) {
  if (table->count == 0) return NULL;
  uint32_t index = hash % table->capacity;
  for (;;) {
	Entry *entry = &table->entries[index];
	if (entry->key == NULL) {
	  // Stop if we find an empty non-tombstone entry.
	  if (IS_NIL(entry->value)) return NULL;
	} else if (entry->key->length == length &&
		entry->key->hash == hash &&
		memcmp(entry->key->value, chars, length) == 0) {
	  // We found it.
	  return entry->key;
	}
	index = (index + 1) % table->capacity;
  }
}

void markTable(VM *vm, Table* table) {
  for (int i = 0; i < table->capacity; i++) {
	Entry* entry = &table->entries[i];
	markObject(vm, (Obj*)entry->key);
	markValue(vm, entry->value);
  }
}

void tableRemoveWhite(Table* table){
  for (int i = 0; i < table->capacity; i++) {
	Entry *entry = &table->entries[i];
	if (entry->key != NULL && !entry->key->obj.isMarked) {
	  tableDelete(table, entry->key);
	}
  }
}