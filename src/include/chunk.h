#ifndef clox_chunk_h
#define clox_chunk_h
#include "common.h"
#include "value.h"


typedef enum
{
    OP_RETURN,
    OP_CONSTANT,
} OpCode;

typedef struct {
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
int getLine(Chunk *chunk, int offset);

#endif