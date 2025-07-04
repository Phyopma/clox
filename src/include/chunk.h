#ifndef clox_chunk_h
#define clox_chunk_h
#include "common.h"
#include "value.h"

typedef enum
{
    OP_RETURN,
    OP_CONSTANT,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_NEGATE,
    OP_NOT,
    OP_CONSTANT_LONG,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_PRINT,
    OP_POP,
    OP_DEFINE_GLOBAL,
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,
    OP_GET_LOCAL,
    OP_SET_LOCAL, 
} OpCode;

typedef struct
{
    int offset;
    int line;
} LineStart;

typedef struct
{
    int count;
    int capacity;
    int lineCount;
    int lineCapacity;
    LineStart *lines;
    uint8_t *code;
    ValueArray constants;
} Chunk;

void initChunk(Chunk *chunk);
void freeChunk(Chunk *chunk);
void writeChunk(Chunk *chunk, uint8_t byte, int line);
int addConstant(Chunk *chunk, Value value);
void writeConstant(Chunk *chunk, Value value, int line);
int getLine(Chunk *chunk, int offset);

#endif