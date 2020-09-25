#ifndef CLOX__COMPILER_H_
#define CLOX__COMPILER_H_

#include "common.h"
#include "clox.h"
#include "object.h"

typedef struct sCompiler Compiler;

ObjFn *compile(VM *vm, const char *source);
void markCompilerRoots(VM *vm, Compiler *compiler);

#endif
