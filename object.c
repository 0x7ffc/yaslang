#include <string.h>
#include <stdio.h>

#include "object.h"
#include "vm.h"
#include "memory.h"

static void initObj(VM *vm, Obj *obj, ObjType type) {
  obj->type = type;
  obj->next = vm->first;
  vm->first = obj;
}

ObjFn *newFn(VM *vm) {
  ObjFn *fn = ALLOCATE(ObjFn, 1);
  initObj(vm, &fn->obj, OBJ_FN);
  fn->arity = 0;
  fn->name = NULL;
  initChunk(&fn->chunk);
  return fn;
}

static ObjString *allocateString(VM *vm, size_t length) {
  ObjString *string = ALLOCATE_FLEX(ObjString, char, length + 1);
  initObj(vm, &string->obj, OBJ_STRING);
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
  tableSet(&vm->strings, string, NIL_VAL);
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
	  printf("<fn %s>", AS_FN(value)->name->value);
	  break;
	}
  }
}

void initTable(Table *table) {
  table->count = 0;
  table->capacity = 0;
  table->entries = NULL;
}

void freeTable(Table *table) {
  FREE(table->entries);
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

static void adjustCapacity(Table *table, int capacity) {
  Entry *entries = ALLOCATE(Entry, capacity);
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
  FREE(table->entries);
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

bool tableSet(Table *table, ObjString *key, Value value) {
  if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
	int capacity = GROW_CAPACITY(table->capacity);
	adjustCapacity(table, capacity);
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

void tableAddAll(Table *from, Table *to) {
  for (int i = 0; i < from->capacity; i++) {
	Entry *entry = &from->entries[i];
	if (entry->key != NULL) {
	  tableSet(to, entry->key, entry->value);
	}
  }
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