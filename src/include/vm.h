#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "value.h"
#include "table.h"
#include "object.h"

#define FRAMES_MAX 64
#define UINT8_COUNT (UINT8_MAX + 1)
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct ObjFunction ObjFunction;
typedef struct ObjClosure ObjClosure;
typedef struct
{
    ObjClosure *closure;
    uint8_t *ip;
    Value *slots;
} CallFrame;

typedef struct
{
    CallFrame frames[FRAMES_MAX];
    int frameCount;
    Chunk *chunk;
    uint8_t *ip;
    Value stack[STACK_MAX];
    Value *stackTop;
    struct ObjUpvalue *openUpvalues;
    Obj *objects;
    Table globals;
    Table strings;
    int grayCapacity;
    int grayCount;
    Obj **grayStack;
    size_t bytesAllocated;
    size_t nextGC;
    ObjString *initString;
} VM;

typedef enum
{
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();
static InterpretResult run();
InterpretResult interpret(const char *source);
void push(Value value);
Value pop();

#endif