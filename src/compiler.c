#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "compiler.h"
#include "object.h"
#include "scanner.h"
#include "chunk.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

#define UINT8_COUNT (UINT8_MAX + 1)

typedef struct
{
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

typedef enum
{
    PREC_NONE,
    PREC_ASSIGNMENT, // =
    PREC_OR,         // or
    PREC_AND,        // and
    PREC_EQUALITY,   // == !=
    PREC_COMPARISON, // < > <= >=
    PREC_TERM,       // + -
    PREC_FACTOR,     // * /
    PREC_UNARY,      // ! -
    PREC_CALL,       // . ()
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);
static void declaration();
static void statement();

static bool identifersEqual(Token *a, Token *b);
static uint8_t identifierConstant(Token *token);
static bool match(TokenType type);
typedef struct
{
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct
{
    Token name;
    bool isConst;
    int depth;
} Local;
typedef struct
{
    Local locals[UINT8_COUNT];
    int localCount;
    int scopeDepth;
} Compiler;

Parser parser;
Compiler *current = NULL;
Chunk *compilingChunk;

static Chunk *currentChunk()
{
    return compilingChunk;
}

static void errorAt(Token *token, const char *message)
{
    if (parser.panicMode)
        return;

    parser.panicMode = true;
    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF)
    {
        fprintf(stderr, " at end");
    }
    else if (token->type == TOKEN_ERROR)
    {
        //
    }
    else
    {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

static void error(const char *message)
{
    errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char *message)
{
    errorAt(&parser.current, message);
}

static void advance()
{
    parser.previous = parser.current;

    for (;;)
    {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR)
            break;
        errorAtCurrent(parser.current.start);
    }
}

static void consume(TokenType type, const char *message)
{
    if (parser.current.type == type)
    {
        advance();
        return;
    }
    errorAtCurrent(message);
}

static void emitByte(uint8_t byte)
{
    writeChunk(currentChunk(), byte, parser.previous.line);
}

// has sideeffect to emitConstant
static void emitBytes(uint8_t byte1, uint8_t byte2)
{
    emitByte(byte1);
    emitByte(byte2);
}

static void emitReturn()
{
    return emitByte(OP_RETURN);
}

static void endCompiler()
{
    emitReturn();

#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError)
    {
        disassembleChunk(currentChunk(), "code");
    }
#endif
}

static void expression();
static ParseRule *getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static void grouping(bool canAssign)
{
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static uint8_t makeConstant(Value value)
{
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT8_MAX)
    {
        error("Too many constants in one chunk");
        return 0;
    }
    return (uint8_t)constant;
}

static void emitConstant(Value value)
{
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT8_MAX)
    {
        uint8_t lower = (constant >> 0) & 0xFF;
        uint8_t middle = (constant >> 8) & 0xFF;
        uint8_t upper = (constant >> 16) & 0xFF;
        emitBytes(OP_CONSTANT_LONG, lower);
        emitBytes(middle, upper);
    }
    else
    {
        emitBytes(OP_CONSTANT, makeConstant(value));
    }
}

static void initCompiler(Compiler *compiler)
{
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    current = compiler;
}

static void number(bool canAssign)
{
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

static void string(bool canAssign)
{
    emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

int resolveLocal(Compiler *compiler, Token *token)
{
    for (int i = compiler->localCount - 1; i >= 0; i--)
    {
        Local *local = &compiler->locals[i];
        if (identifersEqual(token, &local->name))
        {
            if (local->depth == -1)
            {
                error("Can't read local variable in its own intializer.");
            }
            return i;
        }
    }
    return -1;
}

static void namedVariable(Token token, bool canAssign)
{
    uint8_t getOp, setOp;
    int arg = resolveLocal(current, &token);
    if (arg != -1)
    {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    }
    else
    {
        arg = identifierConstant(&token);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }
    if (canAssign && match(TOKEN_EQUAL))
    {
        if (arg != -1 && current->locals[arg].isConst)
        {
            error("Cannot assign to a val variable.");
            return;
        }
        expression();
        emitBytes(setOp, (uint8_t)arg);
    }
    else
        emitBytes(getOp, (uint8_t)arg);
}

static void variable(bool canAssign)
{
    namedVariable(parser.previous, canAssign);
}

static void unary(bool canAssign)
{
    TokenType operatorType = parser.previous.type;

    // Compile operand
    parsePrecedence(PREC_UNARY);

    // emit operator instruction
    switch (operatorType)
    {
    case TOKEN_MINUS:
        emitByte(OP_NEGATE);
        break;
    case TOKEN_BANG:
        emitByte(OP_NOT);
        break;

    default:
        return; // unreachable
    }
}

static void binary(bool canAssign)
{
    TokenType operatorType = parser.previous.type;
    ParseRule *rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));

    switch (operatorType)
    {
    case TOKEN_PLUS:
        emitByte(OP_ADD);
        break;
    case TOKEN_MINUS:
        emitByte(OP_SUBTRACT);
        break;
    case TOKEN_STAR:
        emitByte(OP_MULTIPLY);
        break;
    case TOKEN_SLASH:
        emitByte(OP_DIVIDE);
        break;
    case TOKEN_BANG_EQUAL:
        emitBytes(OP_EQUAL, OP_NOT);
        break;
    case TOKEN_EQUAL_EQUAL:
        emitByte(OP_EQUAL);
        break;
    case TOKEN_GREATER_EQUAL:
        emitBytes(OP_LESS, OP_NOT);
        break;
    case TOKEN_GREATER:
        emitByte(OP_GREATER);
        break;
    case TOKEN_LESS_EQUAL:
        emitBytes(OP_GREATER, OP_NOT);
        break;
    case TOKEN_LESS:
        emitBytes(OP_GREATER, OP_NOT);
        break;
    default:
        return;
    }
}

static void literal(bool canAssign)
{
    switch (parser.previous.type)
    {
    case TOKEN_FALSE:
        emitByte(OP_FALSE);
        break;
    case TOKEN_TRUE:
        emitByte(OP_TRUE);
        break;
    case TOKEN_NIL:
        emitByte(OP_NIL);
        break;
    default:
        return;
    }
}

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN] = {grouping, NULL, PREC_NONE},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT] = {NULL, NULL, PREC_NONE},
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [TOKEN_BANG] = {unary, NULL, PREC_NONE},
    [TOKEN_BANG_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_GREATER] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, NULL, PREC_NONE},
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_FUN] = {NULL, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    [TOKEN_OR] = {NULL, NULL, PREC_NONE},
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_SUPER] = {NULL, NULL, PREC_NONE},
    [TOKEN_THIS] = {NULL, NULL, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [TOKEN_VAR] = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
};

static void parsePrecedence(Precedence precedence)
{
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL)
    {
        error("Expect expression.");
        return;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);

    while (precedence <= getRule(parser.current.type)->precedence)
    {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }
    if (canAssign && match(TOKEN_EQUAL))
        error("Invalid assignment target.");
}

static ParseRule *getRule(TokenType type)
{
    return &rules[type];
}

static void expression()
{
    parsePrecedence(PREC_ASSIGNMENT);
}

static bool check(TokenType type)
{
    return parser.current.type == type;
}

static bool match(TokenType type)
{
    if (!check(type))
        return false;
    advance();
    return true;
}

bool compile(const char *source, Chunk *chunk)
{
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler);
    compilingChunk = chunk;

    parser.hadError = false;
    parser.panicMode = false;
    advance();

    while (!match(TOKEN_EOF))
    {
        declaration();
    }
    endCompiler();
    return !parser.hadError;
}

static void synchronize()
{
    parser.panicMode = false;
    while (parser.current.type != TOKEN_EOF)
    {
        if (parser.previous.type == TOKEN_SEMICOLON)
            return;
        switch (parser.current.type)
        {
        case TOKEN_CLASS:
        case TOKEN_FUN:
        case TOKEN_VAR:
        case TOKEN_FOR:
        case TOKEN_IF:
        case TOKEN_WHILE:
        case TOKEN_PRINT:
        case TOKEN_RETURN:
            return;
        default:;
        }
        advance();
    }
}

static uint8_t identifierConstant(Token *token)
{
    return makeConstant(OBJ_VAL(copyString(token->start, token->length)));
}

static void addLocal(Token name, bool isConst)
{
    if (current->localCount == UINT8_COUNT)
    {
        error("Too many local variables in the function.");
        return;
    }
    Local *local = &current->locals[current->localCount++];
    local->isConst = isConst;
    local->depth = -1;
    local->name = name;
}

static bool identifersEqual(Token *a, Token *b)
{
    if (a->length != b->length)
        return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

static void declareVariable(bool isConst)
{
    if (current->scopeDepth == 0)
        return;
    Token *name = &parser.previous;
    for (int i = current->localCount - 1; i >= 0; i--)
    {
        Local *local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth)
            break;
        if (identifersEqual(name, &local->name))
        {
            error("Already a variable with the same name in this scope.");
        }
    }

    addLocal(*name, isConst);
}

static uint8_t parseVariable(const char *message, bool isConst)
{
    consume(TOKEN_IDENTIFIER, message);
    declareVariable(isConst);
    if (current->scopeDepth > 0)
        return 0;
    return identifierConstant(&parser.previous);
}

static void makeIntialized()
{
    current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static void defineVariable(uint8_t global)
{
    if (current->scopeDepth > 0)
    {
        makeIntialized();
        return;
    }
    emitBytes(OP_DEFINE_GLOBAL, global);
}

static void varDeclaration()
{
    bool isConst = parser.previous.type == TOKEN_VAL;
    uint8_t global = parseVariable("Expect variable name.", isConst);
    if (match(TOKEN_EQUAL))
    {
        expression();
    }
    else
    {
        emitByte(OP_NIL);
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration");
    defineVariable(global);
}

static void declaration()
{
    if (match(TOKEN_VAR) || match(TOKEN_VAL))
    {
        varDeclaration();
    }
    else
    {
        statement();
    }

    if (parser.panicMode)
        synchronize();
}

static void printStatement()
{
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitByte(OP_PRINT);
}

static void expressionStatement()
{
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitByte(OP_POP);
}

static void block()
{
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
    {
        declaration();
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void beginScope()
{
    current->scopeDepth++;
}
static void endScope()
{
    current->scopeDepth--;
    while (current->localCount > 0 && current->locals[current->localCount - 1].depth > current->scopeDepth)
    {
        emitByte(OP_POP);
        current->localCount--;
    }
}

static void statement()
{
    if (match(TOKEN_PRINT))
    {
        printStatement();
    }
    else if (match(TOKEN_LEFT_BRACE))
    {
        beginScope();
        block();
        endScope();
    }
    else
    {
        expressionStatement();
    }
}
