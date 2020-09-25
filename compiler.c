#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"
#include "vm.h"
#include "memory.h"

typedef enum {
  // Single-character tokens.
  TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
  TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
  TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS,
  TOKEN_SEMICOLON, TOKEN_SLASH, TOKEN_STAR,

  // One or two character tokens.
  TOKEN_BANG, TOKEN_BANG_EQUAL,
  TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
  TOKEN_GREATER, TOKEN_GREATER_EQUAL,
  TOKEN_LESS, TOKEN_LESS_EQUAL,

  // Literals.
  TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER,

  // Keywords.
  TOKEN_AND, TOKEN_CLASS, TOKEN_ELSE, TOKEN_FALSE,
  TOKEN_FOR, TOKEN_FUN, TOKEN_IF, TOKEN_NIL, TOKEN_OR,
  TOKEN_PRINT, TOKEN_RETURN, TOKEN_SUPER, TOKEN_THIS,
  TOKEN_TRUE, TOKEN_VAR, TOKEN_WHILE,

  TOKEN_ERROR,
  TOKEN_EOF
} TokenType;

typedef struct {
  TokenType type;
  const char *start;
  int length;
  Value value;
} Token;

typedef struct {
  const char *identifier;
  size_t length;
  TokenType tokenType;
} Keyword;

typedef struct {
  VM *vm;
  const char *source;
  const char *tokenStart;
  const char *currentChar;
  Token current;
  Token previous;
} Parser;

typedef struct {
  Token name;
  int depth;
  bool isCaptured;
} Local;

typedef struct {
  uint8_t index;
  bool isLocal;
} Upvalue;

typedef enum {
  TYPE_FUNCTION,
  TYPE_SCRIPT
} FnType;

struct sCompiler {
  Parser *parser;
  Local locals[UINT8_MAX + 1];
  int localCount;
  Upvalue upvalues[UINT8_MAX + 1];
  int scopeDepth;
  ObjFn *fn;
  FnType type;
  struct sCompiler *parent;
};

typedef enum {
  PREC_NONE,
  PREC_LOWEST,
  PREC_ASSIGNMENT,  // =
  PREC_OR,          // or
  PREC_AND,         // and
  PREC_EQUALITY,    // == !=
  PREC_COMPARISON,  // < > <= >=
  PREC_TERM,        // + -
  PREC_FACTOR,      // * /
  PREC_UNARY,       // ! -
  PREC_CALL,        // . ()
  PREC_PRIMARY
} Precedence;

typedef void (*GrammarFn)(Compiler *compiler, bool canAssign);

typedef struct {
  GrammarFn prefix;
  GrammarFn infix;
  Precedence precedence;
} GrammarRule;

static GrammarRule *getRule(TokenType type);
static void expression(Compiler *compiler);
static void statement(Compiler *compiler);
static void declaration(Compiler *compiler);
static void parsePrecedence(Compiler *compiler, Precedence precedence);

static Keyword keywords[] = {
	{"print", 5, TOKEN_PRINT},
	{"nil", 3, TOKEN_NIL},
	{"fun", 3, TOKEN_FUN},
	{"class", 5, TOKEN_CLASS},
	{"else", 4, TOKEN_ELSE},
	{"false", 5, TOKEN_FALSE},
	{"for", 3, TOKEN_FOR},
	{"if", 2, TOKEN_IF},
	{"return", 6, TOKEN_RETURN},
	{"super", 5, TOKEN_SUPER},
	{"this", 4, TOKEN_THIS},
	{"true", 4, TOKEN_TRUE},
	{"var", 3, TOKEN_VAR},
	{"while", 5, TOKEN_WHILE},
	{NULL, 0, TOKEN_EOF}
};

static Token makeToken(Parser *parser, TokenType type) {
  parser->current.type = type;
  parser->current.start = parser->tokenStart;
  parser->current.length = (int)(parser->currentChar - parser->tokenStart);
}

static char peekChar(Parser *parser) {
  return *parser->currentChar;
}

static bool atEnd(Parser *parser) {
  return peekChar(parser) == '\0';
}

static char peekNextChar(Parser *parser) {
  // If we're at the end of the source, don't read past it.
  if (atEnd(parser)) return '\0';
  return *(parser->currentChar + 1);
}

static char nextChar(Parser *parser) {
  char c = peekChar(parser);
  parser->currentChar++;
  return c;
}

static bool matchChar(Parser *parser, char c) {
  if (peekChar(parser) != c) return false;
  nextChar(parser);
  return true;
}

static void twoCharToken(Parser *parser, char c,
						 TokenType two, TokenType one) {
  makeToken(parser, matchChar(parser, c) ? two : one);
}

static void skipLineComment(Parser *parser) {
  while (peekChar(parser) != '\n' && !atEnd(parser)) {
	nextChar(parser);
  }
}

static void readString(Parser *parser) {
  while (peekChar(parser) != '"' && !atEnd(parser)) {
	nextChar(parser);
  }
  if (matchChar(parser, '"')) {
	parser->current.value = newStringLength(parser->vm, parser->tokenStart + 1,
											(int)(parser->currentChar - parser->tokenStart) - 2);
	makeToken(parser, TOKEN_STRING);
  } else {
	makeToken(parser, TOKEN_ERROR);
  }
}

static bool isName(char c) {
  return (c >= 'a' && c <= 'z' || (c >= 'A' && c <= 'Z') || c == '_');
}

static bool isDigit(char c) {
  return c >= '0' && c <= '9';
}

static void readName(Parser *parser) {
  while (isName(peekChar(parser)) || isDigit(peekChar(parser)))
	nextChar(parser);
  size_t length = parser->currentChar - parser->tokenStart;
  for (int i = 0; keywords[i].identifier != NULL; i++) {
	if (length == keywords[i].length &&
		memcmp(parser->tokenStart, keywords[i].identifier, length) == 0) {
	  makeToken(parser, keywords[i].tokenType);
	  return;
	}
  }
  parser->current.value = newStringLength(parser->vm, parser->tokenStart,
										  (int)(parser->currentChar - parser->tokenStart));
  makeToken(parser, TOKEN_IDENTIFIER);
}

static void readNumber(Parser *parser) {
  while (isDigit(peekChar(parser))) nextChar(parser);
  if (peekChar(parser) == '.' && isDigit(peekNextChar(parser))) {
	nextChar(parser);
	while (isDigit(peekChar(parser))) nextChar(parser);
  }
  parser->current.value = NUM_VAL(strtod(parser->tokenStart, NULL));
  makeToken(parser, TOKEN_NUMBER);
}

static void nextToken(Parser *parser) {
  parser->previous = parser->current;
  if (parser->current.type == TOKEN_EOF) return;
  while (!atEnd(parser)) {
	parser->tokenStart = parser->currentChar;
	char c = nextChar(parser);
	switch (c) {
	  // Skip whitespace.
	  case ' ':
	  case '\r':
	  case '\t':
		while (peekChar(parser) == ' ' ||
			peekChar(parser) == '\r' ||
			peekChar(parser) == '\t') {
		  nextChar(parser);
		}
		break;
		// Skip newline.
	  case '\n': break;
	  case '/':
		if (matchChar(parser, '/')) {
		  skipLineComment(parser);
		  break;
		}
		makeToken(parser, TOKEN_SLASH);
		return;
	  case '(': makeToken(parser, TOKEN_LEFT_PAREN);
		return;
	  case ')': makeToken(parser, TOKEN_RIGHT_PAREN);
		return;
	  case '{': makeToken(parser, TOKEN_LEFT_BRACE);
		return;
	  case '}': makeToken(parser, TOKEN_RIGHT_BRACE);
		return;
	  case ';': makeToken(parser, TOKEN_SEMICOLON);
		return;
	  case ',': makeToken(parser, TOKEN_COMMA);
		return;
	  case '.': makeToken(parser, TOKEN_DOT);
		return;
	  case '-': makeToken(parser, TOKEN_MINUS);
		return;
	  case '+': makeToken(parser, TOKEN_PLUS);
		return;
	  case '*': makeToken(parser, TOKEN_STAR);
		return;
	  case '!': twoCharToken(parser, '=', TOKEN_BANG_EQUAL, TOKEN_BANG);
		return;
	  case '=': twoCharToken(parser, '=', TOKEN_EQUAL_EQUAL, TOKEN_EQUAL);
		return;
	  case '<': twoCharToken(parser, '=', TOKEN_LESS_EQUAL, TOKEN_LESS);
		return;
	  case '>': twoCharToken(parser, '=', TOKEN_GREATER_EQUAL, TOKEN_GREATER);
		return;
	  case '"': readString(parser);
		return;
	  default:
		if (isName(c))
		  readName(parser);
		else if (isDigit(c))
		  readNumber(parser);
		return;
	}
  }
  parser->tokenStart = parser->currentChar;
  makeToken(parser, TOKEN_EOF);
}

static void initCompiler(Compiler *compiler, Parser *parser,
						 Compiler *parent, FnType type) {
  parser->vm->compiler = compiler;
  compiler->parent = parent;
  compiler->parser = parser;
  compiler->localCount = 0;
  compiler->scopeDepth = 0;
  compiler->fn = newFn(parser->vm);
  compiler->type = type;
  if (type != TYPE_SCRIPT) {
	compiler->fn->name = AS_STRING(newStringLength(parser->vm,
												   parser->previous.start,
												   parser->previous.length));
  }
  Local *local = &compiler->locals[compiler->localCount++];
  local->depth = 0;
  local->name.start = "";
  local->name.length = 0;
  local->isCaptured = false;
}

static bool consume(Compiler *compiler, TokenType type) {
  if (compiler->parser->current.type == type) {
	nextToken(compiler->parser);
	return true;
  }
  return false;
}

static void emitByte(Compiler *compiler, uint8_t byte) {
  writeChunk(compiler->parser->vm, &compiler->fn->chunk, byte);
}

static void emitBytes(Compiler *compiler, uint8_t byte1, uint8_t byte2) {
  emitByte(compiler, byte1);
  emitByte(compiler, byte2);
}

static int makeConstant(Compiler *compiler, Value value) {
  return addConstant(compiler->parser->vm, &compiler->fn->chunk, value);
}

void emitConstant(Compiler *compiler, Value value) {
  int index = makeConstant(compiler, value);
  if (index < 256) {
	emitBytes(compiler, OP_CONSTANT, index);
  } else {
	emitBytes(compiler, OP_CONSTANT_LONG, (uint8_t)(index & 0xff));
	emitBytes(compiler, (uint8_t)((index >> 8) & 0xff),
			  (uint8_t)((index >> 16) & 0xff));
  }
}

static void emitReturn(Compiler *compiler) {
  emitByte(compiler, OP_NIL);
  emitByte(compiler, OP_RETURN);
}

static ObjFn *endCompiler(Compiler *compiler) {
  emitReturn(compiler);
  ObjFn *fn = compiler->fn;
  compiler->parser->vm->compiler = compiler->parent;
  return fn;
}

static void unary(Compiler *compiler, bool canAssign) {
  TokenType t = compiler->parser->previous.type;
  parsePrecedence(compiler, PREC_UNARY);
  switch (t) {
	case TOKEN_MINUS: emitByte(compiler, OP_NEGATE);
	  break;
	default: return;
  }
}

static void number(Compiler *compiler, bool canAssign) {
  emitConstant(compiler, compiler->parser->previous.value);
}

static void grouping(Compiler *compiler, bool canAssign) {
  expression(compiler);
  consume(compiler, TOKEN_RIGHT_PAREN);
}

static void binary(Compiler *compiler, bool canAssign) {
  TokenType t = compiler->parser->previous.type;
  GrammarRule *rule = getRule(t);
  parsePrecedence(compiler, rule->precedence);
  switch (t) {
	case TOKEN_PLUS: emitByte(compiler, OP_ADD);
	  break;
	case TOKEN_MINUS: emitByte(compiler, OP_SUBTRACT);
	  break;
	case TOKEN_STAR: emitByte(compiler, OP_MULTIPLY);
	  break;
	case TOKEN_SLASH: emitByte(compiler, OP_DIVIDE);
	  break;
    case TOKEN_LESS: emitByte(compiler, OP_LESS);
      break;
    case TOKEN_EQUAL_EQUAL: emitByte(compiler, OP_EQ);
      break;
	default: return; // Unreachable.
  }
}

static void literal(Compiler *compiler, bool canAssign) {
  switch (compiler->parser->previous.type) {
	case TOKEN_FALSE: emitByte(compiler, OP_FALSE);
	  break;
	case TOKEN_NIL: emitByte(compiler, OP_NIL);
	  break;
	case TOKEN_TRUE: emitByte(compiler, OP_TRUE);
	  break;
	default: return; // Unreachable.
  }
}

static bool identifiersEqual(Token *a, Token *b) {
  if (a->length != b->length) return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler *compiler, Token *name) {
  for (int i = compiler->localCount - 1; i >= 0; i--) {
	Local *local = &compiler->locals[i];
	if (identifiersEqual(name, &local->name)) {
	  return i;
	}
  }
  return -1;
}

static int addUpvalue(Compiler *compiler, uint8_t index, bool isLocal) {
  int upvalueCount = compiler->fn->upvalueCount;
  for (int i = 0; i < upvalueCount; i++) {
    Upvalue *upvalue = &compiler->upvalues[i];
    if (upvalue->index == index && upvalue->isLocal == isLocal) {
      return i;
    }
  }
  //if (upvalueCount == UINT8_MAX + 1) { }
  compiler->upvalues[upvalueCount].isLocal = isLocal;
  compiler->upvalues[upvalueCount].index = index;
  return compiler->fn->upvalueCount++;
}

static int resolveUpvalue(Compiler *compiler, Token* name) {
  if (compiler->parent == NULL) return -1;
  int local = resolveLocal(compiler->parent, name);
  if (local != -1) {
    compiler->parent->locals[local].isCaptured = true;
    return addUpvalue(compiler, (uint8_t)local, true);
  }
  int upvalue = resolveUpvalue(compiler->parent, name);
  if (upvalue != -1) {
    return addUpvalue(compiler, (uint8_t)upvalue, false);
  }
  return -1;
}

static void namedVariable(Compiler *compiler, Token name, bool canAssign) {
  uint8_t getOp, setOp;
  int arg = resolveLocal(compiler, &name);
  if (arg != -1) {
	getOp = OP_GET_LOCAL;
	setOp = OP_SET_LOCAL;
  } else if ((arg = resolveUpvalue(compiler, &name)) != -1) {
    getOp = OP_GET_UPVALUE;
    setOp = OP_SET_UPVALUE;
  } else {
	arg = makeConstant(compiler, name.value);
	getOp = OP_GET_GLOBAL;
	setOp = OP_SET_GLOBAL;
  }
  if (canAssign && consume(compiler, TOKEN_EQUAL)) {
	expression(compiler);
	emitBytes(compiler, setOp, (uint8_t)arg);
  } else {
	emitBytes(compiler, getOp, (uint8_t)arg);
  }
}

static void variable(Compiler *compiler, bool canAssign) {
  namedVariable(compiler, compiler->parser->previous, canAssign);
}

static void string(Compiler *compiler, bool canAssign) {
  emitConstant(compiler, compiler->parser->previous.value);
}

static uint8_t argumentList(Compiler *compiler) {
  uint8_t argCount = 0;
  if (!consume(compiler, TOKEN_RIGHT_PAREN)) {
    do {
      expression(compiler);
      argCount++;
    } while (consume(compiler, TOKEN_COMMA));
  }
  consume(compiler, TOKEN_RIGHT_PAREN);
  return argCount;
}

static void call(Compiler *compiler, bool canAssign) {
  uint8_t argCount = argumentList(compiler);
  emitBytes(compiler, OP_CALL, argCount);
}

GrammarRule rules[] = {
	{grouping, call, PREC_CALL},   // TOKEN_LEFT_PAREN
	{NULL, NULL, PREC_NONE},       // TOKEN_RIGHT_PAREN
	{NULL, NULL, PREC_NONE},       // TOKEN_LEFT_BRACE
	{NULL, NULL, PREC_NONE},       // TOKEN_RIGHT_BRACE
	{NULL, NULL, PREC_NONE},       // TOKEN_COMMA
	{NULL, NULL, PREC_NONE},       // TOKEN_DOT
	{unary, binary, PREC_TERM},    // TOKEN_MINUS
	{NULL, binary, PREC_TERM},     // TOKEN_PLUS
	{NULL, NULL, PREC_NONE},       // TOKEN_SEMICOLON
	{NULL, binary, PREC_FACTOR},   // TOKEN_SLASH
	{NULL, binary, PREC_FACTOR},   // TOKEN_STAR
	{NULL, NULL, PREC_NONE},       // TOKEN_BANG
	{NULL, binary, PREC_EQUALITY}, // TOKEN_BANG_EQUAL
	{NULL, NULL, PREC_NONE},       // TOKEN_EQUAL
	{NULL, binary,  PREC_EQUALITY },   // TOKEN_EQUAL_EQUAL
	{NULL, binary,  PREC_COMPARISON }, // TOKEN_GREATER
	{NULL, binary,  PREC_COMPARISON }, // TOKEN_GREATER_EQUAL
	{NULL, binary,  PREC_COMPARISON }, // TOKEN_LESS
	{NULL, binary,  PREC_COMPARISON }, // TOKEN_LESS_EQUAL
	{variable, NULL, PREC_NONE},   // TOKEN_IDENTIFIER
	{string, NULL, PREC_NONE},     // TOKEN_STRING
	{number, NULL, PREC_NONE},     // TOKEN_NUMBER
	{NULL, NULL, PREC_NONE},       // TOKEN_AND
	{NULL, NULL, PREC_NONE},       // TOKEN_CLASS
	{NULL, NULL, PREC_NONE},       // TOKEN_ELSE
	{literal, NULL, PREC_NONE},    // TOKEN_FALSE
	{NULL, NULL, PREC_NONE},       // TOKEN_FOR
	{NULL, NULL, PREC_NONE},       // TOKEN_FUN
	{NULL, NULL, PREC_NONE},       // TOKEN_IF
	{literal, NULL, PREC_NONE},    // TOKEN_NIL
	{NULL, NULL, PREC_NONE},       // TOKEN_OR
	{NULL, NULL, PREC_NONE},       // TOKEN_PRINT
	{NULL, NULL, PREC_NONE},       // TOKEN_RETURN
	{NULL, NULL, PREC_NONE},       // TOKEN_SUPER
	{NULL, NULL, PREC_NONE},       // TOKEN_THIS
	{literal, NULL, PREC_NONE},    // TOKEN_TRUE
	{NULL, NULL, PREC_NONE},       // TOKEN_VAR
	{NULL, NULL, PREC_NONE},       // TOKEN_WHILE
	{NULL, NULL, PREC_NONE},       // TOKEN_ERROR
	{NULL, NULL, PREC_NONE},       // TOKEN_EOF
};

static void parsePrecedence(Compiler *compiler, Precedence precedence) {
  nextToken(compiler->parser);
  GrammarFn prefix = getRule(compiler->parser->previous.type)->prefix;
  if (prefix == NULL)
	return;
  bool canAssign = precedence <= PREC_ASSIGNMENT;
  prefix(compiler, canAssign);
  while (getRule(compiler->parser->current.type)->precedence > precedence) {
	nextToken(compiler->parser);
	GrammarFn infix = getRule(compiler->parser->previous.type)->infix;
	infix(compiler, canAssign);
  }
  if (canAssign && consume(compiler, TOKEN_EQUAL)) {
	printf("Invalid assignment target.");
  }
}

static GrammarRule *getRule(TokenType type) {
  return &rules[type];
}

static void printStatement(Compiler *compiler) {
  expression(compiler);
  consume(compiler, TOKEN_SEMICOLON);
  emitByte(compiler, OP_PRINT);
}

static void expressionStatement(Compiler *compiler) {
  expression(compiler);
  consume(compiler, TOKEN_SEMICOLON);
  emitByte(compiler, OP_POP);
}

static void addLocal(Compiler *compiler, Token name) {
  if (compiler->localCount == UINT8_MAX + 1) return;
  Local *local = &compiler->locals[compiler->localCount++];
  local->name = name;
  local->depth = -1;
  local->isCaptured = false;
}

// For local only.
static void markInitialized(Compiler *compiler) {
  if (compiler->scopeDepth == 0) return;
  compiler->locals[compiler->localCount - 1].depth = compiler->scopeDepth;
}

// For local only.
static void declareVariable(Compiler *compiler) {
  if (compiler->scopeDepth == 0) return;
  Token name = compiler->parser->previous;
  addLocal(compiler, name);
}

static uint8_t parseVariable(Compiler *compiler) {
  consume(compiler, TOKEN_IDENTIFIER);
  declareVariable(compiler);
  if (compiler->scopeDepth > 0) return 0;
  return makeConstant(compiler, compiler->parser->previous.value);
}

static void defineVariable(Compiler *compiler, uint8_t global) {
  if (compiler->scopeDepth > 0) {
	markInitialized(compiler);
	return;
  }
  emitBytes(compiler, OP_DEFINE_GLOBAL, global);
}

static void varDeclaration(Compiler *compiler) {
  uint8_t global = parseVariable(compiler);
  if (consume(compiler, TOKEN_EQUAL)) {
	expression(compiler);
  } else {
	emitByte(compiler, OP_NIL);
  }
  consume(compiler, TOKEN_SEMICOLON);
  defineVariable(compiler, global);
}

static void block(Compiler *compiler) {
  while (compiler->parser->current.type != TOKEN_RIGHT_BRACE
	  && compiler->parser->current.type != TOKEN_EOF) {
	declaration(compiler);
  }
  consume(compiler, TOKEN_RIGHT_BRACE);
}

static void beginScope(Compiler *compiler) {
  compiler->scopeDepth++;
}

static void endScope(Compiler *compiler) {
  compiler->scopeDepth--;
  while (compiler->localCount > 0 &&
	  compiler->locals[compiler->localCount - 1].depth >
		  compiler->scopeDepth) {
	if (compiler->locals[compiler->localCount - 1].isCaptured) {
	  emitByte(compiler, OP_CLOSE_UPVALUE);
	} else {
	  emitByte(compiler, OP_POP);
	}
	compiler->localCount--;
  }
}

static int emitJump(Compiler *compiler, uint8_t instruction) {
  emitByte(compiler, instruction);
  emitByte(compiler, 0xff);
  emitByte(compiler, 0xff);
  return compiler->fn->chunk.count - 2;
}

static void patchJump(Compiler *compiler, int offset) {
  Chunk *chunk = &compiler->fn->chunk;
  int jump = chunk->count - offset - 2;
  chunk->code[offset] = (jump >> 8) & 0xff;
  chunk->code[offset + 1] = jump & 0xff;
}

static void ifStatement(Compiler *compiler) {
  consume(compiler, TOKEN_LEFT_PAREN);
  expression(compiler);
  consume(compiler, TOKEN_RIGHT_PAREN);
  int thenJump = emitJump(compiler, OP_JUMP_IF);
  statement(compiler);
  if (consume(compiler, TOKEN_ELSE)) {
	int elseJump = emitJump(compiler, OP_JUMP);
	patchJump(compiler, thenJump);
	statement(compiler);
	patchJump(compiler, elseJump);
  } else {
	patchJump(compiler, thenJump);
  }
}

static void emitLoop(Compiler *compiler, int loopStart) {
  emitByte(compiler, OP_LOOP);
  int offset = compiler->fn->chunk.count - loopStart + 2;
  emitByte(compiler, (offset >> 8) & 0xff);
  emitByte(compiler, offset & 0xff);
}

static void whileStatement(Compiler *compiler) {
  int loopStart = compiler->fn->chunk.count;
  consume(compiler, TOKEN_LEFT_PAREN);
  expression(compiler);
  consume(compiler, TOKEN_RIGHT_PAREN);
  int exitJump = emitJump(compiler, OP_JUMP_IF);
  statement(compiler);
  emitLoop(compiler, loopStart);
  patchJump(compiler, exitJump);
}

static void function(Compiler *compiler, FnType type) {
  Compiler fnCompiler;
  initCompiler(&fnCompiler, compiler->parser, compiler, type);
  beginScope(&fnCompiler);
  consume(&fnCompiler, TOKEN_LEFT_PAREN);
  if (!consume(&fnCompiler, TOKEN_RIGHT_PAREN)) {
    do {
      fnCompiler.fn->arity++;
      uint8_t paramConstant = parseVariable(&fnCompiler);
      defineVariable(&fnCompiler, paramConstant);
    } while (consume(&fnCompiler, TOKEN_COMMA));
  }
  consume(&fnCompiler, TOKEN_RIGHT_PAREN);
  consume(&fnCompiler, TOKEN_LEFT_BRACE);
  block(&fnCompiler);
  ObjFn *fn = endCompiler(&fnCompiler);
  emitBytes(compiler, OP_CLOSURE, makeConstant(compiler, OBJ_VAL(fn)));
  for (int i = 0; i < fn->upvalueCount; i++) {
    emitByte(compiler, fnCompiler.upvalues[i].isLocal ? 1 : 0);
    emitByte(compiler, fnCompiler.upvalues[i].index);
  }
}

static void funDeclaration(Compiler *compiler) {
  uint8_t global = parseVariable(compiler);
  markInitialized(compiler);
  function(compiler, TYPE_FUNCTION);
  defineVariable(compiler, global);
}

static void returnStatement(Compiler *compiler) {
  //if (compiler->type == TYPE_SCRIPT) {}
  if (consume(compiler, TOKEN_SEMICOLON)) {
    emitReturn(compiler);
  } else {
    expression(compiler);
    consume(compiler, TOKEN_SEMICOLON);
	if (compiler->fn->chunk.count >= 2  &&
	    compiler->fn->chunk.code[compiler->fn->chunk.count - 2] == OP_CALL) {
	  compiler->fn->chunk.code[compiler->fn->chunk.count - 2] = OP_TAIL_CALL;
	} else {
	  emitByte(compiler, OP_RETURN);
	}
  }
}

static void expression(Compiler *compiler) {
  parsePrecedence(compiler, PREC_LOWEST);
}

static void declaration(Compiler *compiler) {
  if (consume(compiler, TOKEN_FUN)) {
	funDeclaration(compiler);
  } else if (consume(compiler, TOKEN_VAR)) {
	varDeclaration(compiler);
  } else {
	statement(compiler);
  }
}

static void statement(Compiler *compiler) {
  if (consume(compiler, TOKEN_PRINT)) {
	printStatement(compiler);
  } else if (consume(compiler, TOKEN_LEFT_BRACE)) {
	beginScope(compiler);
	block(compiler);
	endScope(compiler);
  } else if (consume(compiler, TOKEN_IF)) {
	ifStatement(compiler);
  } else if (consume(compiler, TOKEN_WHILE)) {
	whileStatement(compiler);
  } else if (consume(compiler, TOKEN_RETURN)) {
    returnStatement(compiler);
  } else {
	expressionStatement(compiler);
  }
}

ObjFn *compile(VM *vm, const char *source) {
  Parser parser;
  parser.vm = vm;
  parser.source = source;
  parser.tokenStart = source;
  parser.currentChar = source;
  parser.current.type = TOKEN_ERROR;
  parser.current.start = source;
  parser.current.length = 0;
  Compiler compiler;
  initCompiler(&compiler, &parser, NULL, TYPE_SCRIPT);
  /*for (;;) {
	nextToken(&parser);
	printf("%2d '%.*s'\n", parser.current.type, parser.current.length, parser.current.start);
	if (parser.current.type == TOKEN_EOF) break;
  }*/
  nextToken(&parser);
  while (!consume(&compiler, TOKEN_EOF)) {
	declaration(&compiler);
  }
  return endCompiler(&compiler);
}

void markCompilerRoots(VM *vm, Compiler *compiler) {
  Compiler* c = compiler;
  while (c != NULL) {
	markObject(vm, (Obj*)c->fn);
	c = c->parent;
  }
}