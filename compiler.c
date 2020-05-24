#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"
#include "value.h"

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
  const char *source;
  const char *tokenStart;
  const char *currentChar;
  Token current;
  Token previous;
} Parser;

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

typedef void (*GrammarFn)(Parser *parser);

typedef struct {
  GrammarFn prefix;
  GrammarFn infix;
  Precedence precedence;
} GrammarRule;

static GrammarRule *getRule(TokenType type);
static void expression(Parser *parser);
static void parsePrecedence(Parser *parser, Precedence precedence);

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
  if (matchChar(parser, '"'))
	makeToken(parser, TOKEN_STRING);
  else
	makeToken(parser, TOKEN_ERROR);
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
  makeToken(parser, TOKEN_IDENTIFIER);
}

static void readNumber(Parser *parser) {
  while (isDigit(peekChar(parser))) nextChar(parser);
  if (peekChar(parser) == '.' && isDigit(peekNextChar(parser))) {
	nextChar(parser);
	while (isDigit(peekChar(parser))) nextChar(parser);
  }
  parser->current.value = strtod(parser->tokenStart, NULL);
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

static bool consume(Parser *parser, TokenType type) {
  if (parser->current.type == type) {
	nextToken(parser);
	return true;
  }
  return false;
}

Chunk* compilingChunk;

static Chunk *currentChunk() {
  return compilingChunk;
}

static void emitByte(uint8_t byte) {
  writeChunk(currentChunk(), byte);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
  emitByte(byte1);
  emitByte(byte2);
}

void emitConstant(Value value) {
  int index = addConstant(currentChunk(), value);
  if (index < 256) {
    emitBytes(OP_CONSTANT, index);
  } else {
    emitBytes(OP_CONSTANT_LONG, (uint8_t)(index & 0xff));
    emitBytes((uint8_t)((index >> 8) & 0xff),
    	(uint8_t)((index >> 16) & 0xff));
  }
}

static void emitReturn() {
  emitByte(OP_RETURN);
}

static void endCompiler() {
  emitReturn();
}

static void unary(Parser *parser) {
  TokenType t = parser->previous.type;
  parsePrecedence(parser, PREC_UNARY);
  switch (t) {
    case TOKEN_MINUS: emitByte(OP_NEGATE); break;
	default:
	  return;
  }
}

static void number(Parser *parser) {
  emitConstant(parser->previous.value);
}

static void grouping(Parser *parser) {
  expression(parser);
  consume(parser, TOKEN_RIGHT_PAREN);
}

static void binary(Parser *parser) {
  TokenType t = parser->previous.type;
  GrammarRule* rule = getRule(t);
  parsePrecedence(parser, rule->precedence);
  switch (t) {
	case TOKEN_PLUS:  emitByte(OP_ADD); break;
	case TOKEN_MINUS: emitByte(OP_SUBTRACT); break;
	case TOKEN_STAR:  emitByte(OP_MULTIPLY); break;
	case TOKEN_SLASH: emitByte(OP_DIVIDE); break;
	default:
	  return; // Unreachable.
  }
}

GrammarRule rules[] = {
	{ grouping, NULL,    PREC_NONE },       // TOKEN_LEFT_PAREN
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_RIGHT_PAREN
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_LEFT_BRACE
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_RIGHT_BRACE
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_COMMA
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_DOT
	{ unary,    binary,  PREC_TERM },       // TOKEN_MINUS
	{ NULL,     binary,  PREC_TERM },       // TOKEN_PLUS
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_SEMICOLON
	{ NULL,     binary,  PREC_FACTOR },     // TOKEN_SLASH
	{ NULL,     binary,  PREC_FACTOR },     // TOKEN_STAR
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_BANG
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_BANG_EQUAL
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_EQUAL
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_EQUAL_EQUAL
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_GREATER
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_GREATER_EQUAL
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_LESS
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_LESS_EQUAL
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_IDENTIFIER
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_STRING
	{ number,   NULL,    PREC_NONE },       // TOKEN_NUMBER
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_AND
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_CLASS
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_ELSE
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_FALSE
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_FOR
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_FUN
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_IF
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_NIL
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_OR
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_PRINT
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_RETURN
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_SUPER
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_THIS
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_TRUE
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_VAR
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_WHILE
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_ERROR
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_EOF
};

static void parsePrecedence(Parser *parser, Precedence precedence) {
  nextToken(parser);
  GrammarFn prefix = getRule(parser->previous.type)->prefix;
  if (prefix == NULL)
    return;
  prefix(parser);
  while (getRule(parser->current.type)->precedence > precedence) {
    nextToken(parser);
    GrammarFn infix = getRule(parser->previous.type)->infix;
    infix(parser);
  }
}

static GrammarRule *getRule(TokenType type) {
  return &rules[type];
}

static void expression(Parser *parser) {
  parsePrecedence(parser, PREC_LOWEST);
}

bool compile(const char *source, Chunk *chunk) {
  Parser parser;
  parser.source = source;
  parser.tokenStart = source;
  parser.currentChar = source;
  compilingChunk = chunk;
  /*for (;;) {
	nextToken(&parser);
	printf("%2d '%.*s'\n", parser.current.type, parser.current.length, parser.current.start);
	if (parser.current.type == TOKEN_EOF) break;
  }*/
  nextToken(&parser);
  expression(&parser);
  consume(&parser, TOKEN_EOF);
  endCompiler();
  return true;
}