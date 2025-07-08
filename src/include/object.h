#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "vm.h"
#include "chunk.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_STRING(value) isObjType(value, OBJ_STRING)
#define AS_FUNCTION(value) ((ObjFunction *)AS_OBJ(value))
#define AS_NATIVE(value) (((ObjNative *)AS_OBJ(value))->function)
#define AS_STRING(value) ((ObjString *)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString *)AS_OBJ(value))->chars)

ObjString *copyString(const char *start, int length);
ObjString *takeString(char *chars, int length);
ObjString *allocateString(char *chars, int length, bool ownChars, uint32_t hash);

typedef enum
{
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_NATIVE,
} ObjType;

struct Obj
{
    ObjType type;
    struct Obj *next;
};

typedef Value (*NativeFn)(int argCount, Value *arg);
typedef struct
{
    Obj obj;
    NativeFn function;
} ObjNative;

typedef struct ObjString
{
    Obj obj;
    int length;
    uint32_t hash;
    bool ownChars;
    char *chars;
} ObjString;

typedef struct ObjFunction
{
    Obj obj;
    int arity;
    Chunk chunk;
    ObjString *name;
} ObjFunction;

ObjFunction *newFunction();
ObjNative *newNative(NativeFn function);

static bool isObjType(Value value, ObjType type)
{
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

void printObject(Value value);

#endif
