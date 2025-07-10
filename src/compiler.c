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
static void declareVariable(bool isConst);
static void defineVariable(uint8_t global);
static bool identifersEqual(Token *a, Token *b);
static uint8_t identifierConstant(Token *token);
static bool match(TokenType type);
static int emitJump(uint8_t instruction);
static void patchJump(int offset);
static void ifStatement();
static void whileStatement();
static void forStatement();
static void continueStatement();
static void switchStatement();
static void returnStatement();
static bool check(TokenType type);

typedef struct
{
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct Loop
{
    struct Loop *enclosing;
    int loopStart;
    int scopeDepth;
} Loop;

typedef struct
{
    Token name;
    bool isConst;
    int depth;
} Local;

typedef enum
{
    TYPE_FUNCTION,
    TYPE_SCRIPT,
} FunctionType;
typedef struct Compiler
{
    struct Compiler *enclosing;
    ObjFunction *function;
    FunctionType type;

    Local locals[UINT8_COUNT];
    Loop *loop;
    int localCount;
    int scopeDepth;
} Compiler;

Parser parser;
Compiler *current = NULL;
Chunk *compilingChunk;

static Chunk *currentChunk()
{
    return &current->function->chunk;
}

static void errorAt(Token *token, const char *message)
{
    if (parser.panicMode)
    {
        return;
    }

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

    emitByte(OP_NIL);
    emitByte(OP_RETURN);
}

static ObjFunction *endCompiler()
{
    emitReturn();
    ObjFunction *function = current->function;

#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError)
    {
        disassembleChunk(currentChunk(), function->name != NULL ? function->name->chars : "<script>");
    }
#endif
    current = current->enclosing;
    return function;
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

static void initCompiler(Compiler *compiler, FunctionType type)
{
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->function = newFunction();
    compiler->loop = NULL;
    current = compiler;

    if (type != TYPE_SCRIPT)
    {
        current->function->name = copyString(parser.previous.start, parser.previous.length);
    }

    Local *local = &current->locals[current->localCount++];
    local->depth = 0;
    local->name.start = "";
    local->name.length = 0;
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

static uint8_t argumentList()
{
    uint8_t argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN))
    {
        do
        {
            expression();
            if (argCount == 255)
            {
                error("Can't have more than 255 arguments.");
            }
            argCount++;
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}

static void call(bool canAssign)
{
    uint8_t argCount = argumentList();
    emitBytes(OP_CALL, argCount);
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
        emitByte(OP_LESS);
        break;
    default:
        return;
    }
}

static void and_(bool canAssign)
{
    int endJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    parsePrecedence(PREC_AND);
    patchJump(endJump);
}

static void or_(bool canAssign)
{
    int elseJump = emitJump(OP_JUMP_IF_FALSE);
    int endJump = emitJump(OP_JUMP);

    patchJump(elseJump);
    emitByte(OP_POP);

    parsePrecedence(PREC_OR);
    patchJump(endJump);

    //    (true or ......)
    //    (false or ....)
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
    [TOKEN_LEFT_PAREN] = {grouping, call, PREC_CALL},
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
    [TOKEN_AND] = {NULL, and_, PREC_AND},
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_FUN] = {NULL, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    [TOKEN_OR] = {NULL, or_, PREC_OR},
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

ObjFunction *compile(const char *source)
{
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);

    parser.hadError = false;
    parser.panicMode = false;
    advance();

    while (!match(TOKEN_EOF))
    {
        declaration();
    }
    ObjFunction *function = endCompiler();
    return parser.hadError ? NULL : function;
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
        case TOKEN_CASE:
        case TOKEN_DEFAULT:
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
    if (current->scopeDepth == 0)
        return;
    current->locals[current->localCount - 1].depth = current->scopeDepth;
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

static void function(FunctionType type)
{
    Compiler compiler;
    initCompiler(&compiler, type);

    beginScope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    if (!check(TOKEN_RIGHT_PAREN))
    {
        do
        {
            current->function->arity++;
            if (current->function->arity > 255)
            {
                errorAtCurrent("Can't have more than 255 parameters.");
            }
            uint8_t constant = parseVariable("Expect parameter name.", false);
            defineVariable(constant);

        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");

    block();

    ObjFunction *function = endCompiler();
    emitBytes(OP_CONSTANT, makeConstant(OBJ_VAL(function)));

    // endScope(); endCompiler handle that
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

static void funDeclaration()
{
    uint8_t global = parseVariable("Expect function name.", true);
    makeIntialized();
    function(TYPE_FUNCTION);
    defineVariable(global);
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
    if (match(TOKEN_FUN))
    {

        funDeclaration();
    }
    else if (match(TOKEN_VAR) || match(TOKEN_VAL))
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

static void statement()
{
    if (match(TOKEN_PRINT))
    {
        printStatement();
    }
    else if (match(TOKEN_IF))
    {
        ifStatement();
    }
    else if (match(TOKEN_WHILE))
    {
        whileStatement();
    }
    else if (match(TOKEN_FOR))
    {
        forStatement();
    }
    else if (match(TOKEN_CONTINUE))
    {
        continueStatement();
    }
    else if (match(TOKEN_SWITCH))
    {
        switchStatement();
    }
    else if (match(TOKEN_LEFT_BRACE))
    {
        beginScope();
        block();
        endScope();
    }
    else if (match(TOKEN_RETURN))
    {
        returnStatement();
    }
    else
    {
        expressionStatement();
    }
}

static int emitJump(uint8_t instruction)
{
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count - 2;
}

static void patchJump(int offset)
{
    int jump = currentChunk()->count - offset - 2;

    if (jump > INT16_MAX)
    {
        error("Too much code to jump.");
    }

    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

static void ifStatement()
{
    consume(TOKEN_LEFT_PAREN, "Expect '(' after if.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after conidition");

    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);

    statement();

    int elseJump = emitJump(OP_JUMP);

    patchJump(thenJump);
    emitByte(OP_POP);

    if (match(TOKEN_ELSE))
        statement();
    patchJump(elseJump);
}

static void emitLoop(int loopStart)
{
    emitByte(OP_LOOP);
    int offset = currentChunk()->count - loopStart + 2;

    if (offset > INT16_MAX)
    {
        error("Loop body too large.");
    }
    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

static void whileStatement()
{
    // int loopStart = currentChunk()->count;
    Loop loop;
    loop.enclosing = current->loop;
    current->loop = &loop;

    loop.loopStart = currentChunk()->count;
    loop.scopeDepth = current->scopeDepth;
    // revise this in case

    consume(TOKEN_LEFT_PAREN, "Expect '(' after while.)");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after while condition.");

    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);

    statement();
    emitLoop(loop.loopStart); // changed here

    patchJump(exitJump);
    emitByte(OP_POP);

    current->loop = loop.enclosing;
}

static void forStatement()
{
    beginScope();
    consume(TOKEN_LEFT_PAREN, "Expect '(' after for.");
    if (match(TOKEN_SEMICOLON))
    {
        // do nothing
    }
    else if (match(TOKEN_VAR))
    {
        varDeclaration();
    }
    else
    {
        expressionStatement();
    }
    // end of intializer

    int loopStart = currentChunk()->count; // loop beginning
    int exitJump = -1;                     // exit offset after falsey condition

    if (!match(TOKEN_SEMICOLON))
    {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

        // jump out of loop for falsey condition
        exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP); // condition
    }
    // end of condition

    if (!match(TOKEN_RIGHT_PAREN))
    {
        int bodyJump = emitJump(OP_JUMP);
        int incrementStart = currentChunk()->count;
        expression();
        emitByte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expcet ')' at the end.");
        emitLoop(loopStart);        // loop from end of increment to condition
        loopStart = incrementStart; // if increment exists, emitLoop jumps back to incrementStart
        patchJump(bodyJump);
    }

    Loop loop;
    loop.enclosing = current->loop;
    current->loop = &loop;

    loop.loopStart = loopStart;
    loop.scopeDepth = current->scopeDepth;

    statement();         // while body
    emitLoop(loopStart); // end of while,
    // will direct back to increment clause begining if exist,
    // else beginning of condition clause

    // if condition clause exits
    if (exitJump != -1)
    {
        patchJump(exitJump);
        emitByte(OP_POP); // Condition
    }
    current->loop = loop.enclosing;
    endScope();
}

static void continueStatement()
{
    if (current->loop == NULL)
    {
        error("Can't use 'continue' outside of a loop.");
        return;
    }

    consume(TOKEN_SEMICOLON, "Expect ';' after 'continue'.");

    // poping locals created inside the loop
    for (int i = current->localCount - 1; i >= 0 && current->locals[i].depth > current->loop->scopeDepth; i--)
    {
        emitByte(OP_POP);
    }

    emitLoop(current->loop->loopStart);
}

static int caseStatement()
{
    expression();
    consume(TOKEN_COLON, "Expect ':' after expression in case statement.");
    int nextCase = emitJump(OP_CASE);
    statement();
    if (!check(TOKEN_CASE) && !check(TOKEN_DEFAULT) && !check(TOKEN_RIGHT_BRACE))
    {
        errorAtCurrent("Expect block. use '{' and '}' for block statement.");
    }
    int endJump = emitJump(OP_JUMP);
    patchJump(nextCase);
    return endJump;
}

static void switchStatement()
{
    consume(TOKEN_LEFT_PAREN, "Expect '(' after switch.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression in switch.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before switch cases.");

    int caseJumps[UINT8_COUNT];
    int caseCount = 0;
    bool defaultCase = false;

    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
    {
        if (match(TOKEN_CASE))
        {
            if (caseCount < UINT8_COUNT)
            {
                caseJumps[caseCount++] = caseStatement();
            }
            else
            {
                error("Too many switch cases. Ignoring extra cases.");
            }
        }
        else if (match(TOKEN_DEFAULT))
        {
            if (defaultCase)
            {
                error("Can't have more than one default case.");
            }
            consume(TOKEN_COLON, "Expect ':' after default.");
            defaultCase = true;
            statement();
            if (!check(TOKEN_CASE) && !check(TOKEN_DEFAULT) && !check(TOKEN_RIGHT_BRACE))
            {
                errorAtCurrent("Expect block. use '{' and '}' for block statement.");
            }
        }
        else
        {
            errorAtCurrent("Expect 'case' or 'default' in switch statement.");
            while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF) &&
                   !check(TOKEN_CASE) && !check(TOKEN_DEFAULT))
            {
                advance();
            }
        }
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after switch cases.");
    emitByte(OP_POP);

    for (int i = 0; i < caseCount; i++)
    {
        patchJump(caseJumps[i]);
    }
}

static void returnStatement()
{
    if (current->type == TYPE_SCRIPT)
    {
        error("Can't return from top-level code.");
    }
    if (match(TOKEN_SEMICOLON))
    {
        emitReturn();
    }
    else
    {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ':' after return value.");
        emitByte(OP_RETURN);
    }
}
