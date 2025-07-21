#include <stdlib.h>
#include "memory.h"
#include "vm.h"

#include "chunk.h"

void initChunk(Chunk *chunk)
{
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lineCount = 0;
    chunk->lineCapacity = 0;
    chunk->lines = NULL;
    initValueArray(&chunk->constants);
}

void freeChunk(Chunk *chunk)
{
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    freeValueArray(&chunk->constants);
    FREE_ARRAY(LineStart, chunk->lines, chunk->lineCapacity);
    initChunk(chunk);
}

void writeChunk(Chunk *chunk, uint8_t byte, int line)
{

    if (chunk->capacity < chunk->count + 1)
    {
        int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCapacity, chunk->capacity);
    }
    chunk->code[chunk->count] = byte;
    if (!(chunk->lineCount > 0 && chunk->lines[chunk->lineCount - 1].line == line))
    {
        if (chunk->lineCapacity < chunk->lineCount + 1)
        {
            int oldCapacity = chunk->lineCapacity;
            chunk->lineCapacity = GROW_CAPACITY(oldCapacity);
            chunk->lines = GROW_ARRAY(LineStart, chunk->lines, oldCapacity, chunk->lineCapacity);
        }

        chunk->lines[chunk->lineCount].offset = chunk->count;
        chunk->lines[chunk->lineCount].line = line;
        chunk->lineCount++;
    }
    chunk->count++;
}

int addConstant(Chunk *chunk, Value value)
{
    push(value);
    writeValueArray(&chunk->constants, value);
    pop();
    return chunk->constants.count - 1;
}

void writeConstant(Chunk *chunk, Value value, int line)
{
    int constantIndex = addConstant(chunk, value);
    if (constantIndex < 256)
    {
        writeChunk(chunk, OP_CONSTANT, line);
        writeChunk(chunk, constantIndex, line);
    }
    else
    {
        writeChunk(chunk, OP_CONSTANT_LONG, line);
        // little endian
        // write lower bytes
        writeChunk(chunk, (constantIndex >> 0) & 0xFF, line);
        // write middle bytes
        writeChunk(chunk, (constantIndex >> 8) & 0xFF, line);
        // write upper bytes
        writeChunk(chunk, (constantIndex >> 16) & 0xFF, line);
    }
}

int getLine(Chunk *chunk, int offset)
{
    for (int i = chunk->lineCount - 1; i >= 0; i--)
    {
        // cuz we save staring offest for each line
        if (chunk->lines[i].offset <= offset)
        {
            return chunk->lines[i].line;
        }
    }

    return -1;
}