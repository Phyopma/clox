#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "vm.h"
#include "table.h"
#include "chunk.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_STRING(value) isObjType(value, OBJ_STRING)
#define IS_CLOSURE(value) isObjType(value, OBJ_CLOSURE)
#define IS_CLASS(value) isObjType(value, OBJ_CLASS)
#define IS_INSTANCE(value) isObjType(value, OBJ_INSTANCE)
#define IS_OBJ_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)
#define AS_FUNCTION(value) ((ObjFunction *)AS_OBJ(value))
#define AS_NATIVE(value) ((AS_NATIVE_OBJ)->function)
#define AS_NATIVE_OBJ(value) ((ObjNative *)AS_OBJ(value))
#define AS_STRING(value) ((ObjString *)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString *)AS_OBJ(value))->chars)
#define AS_CLOSURE(value) ((ObjClosure *)AS_OBJ(value))
#define AS_CLASS(value) ((ObjClass *)AS_OBJ(value))
#define AS_INSTANCE(value) ((ObjInstance *)AS_OBJ(value))
#define AS_BOUND_METHOD(value) ((ObjBoundMethod *)AS_OBJ(value))

typedef enum
{
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_CLOSURE,
    OBJ_UPVALUE,
    OBJ_CLASS,
    OBJ_INSTANCE,
    OBJ_BOUND_METHOD,
} ObjType;

struct Obj
{
    ObjType type;
    struct Obj *next;
    bool isMarked;
};

typedef bool (*NativeFn)(int argCount, Value *arg, Value *result);
typedef struct
{
    Obj obj;
    int arity;
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

typedef struct ObjUpvalue
{
    Obj obj;
    Value *location;
    Value closed;
    struct ObjUpvalue *next;
} ObjUpvalue;

typedef struct ObjFunction
{
    Obj obj;
    int arity;
    int upvalueCount;
    Chunk chunk;
    ObjString *name;
} ObjFunction;

typedef struct ObjClosure
{
    Obj obj;
    ObjUpvalue **upvalues;
    int upvalueCount;
    ObjFunction *function;
} ObjClosure;

typedef struct
{
    Obj obj;
    ObjString *name;
    Table methods;
} ObjClass;

typedef struct
{
    Obj obj;
    ObjClass *klass;
    Table fields;
} ObjInstance;

typedef struct
{
    Obj obj;
    Value receiver;
    ObjClosure *method;
} ObjBoundMethod;

ObjFunction *
newFunction();
ObjNative *newNative(NativeFn function, int arity);
ObjClosure *newClosure(ObjFunction *function);
ObjClass *newClass(ObjString *name);
ObjInstance *newInstance(ObjClass *klass);
ObjString *copyString(const char *start, int length);
ObjUpvalue *newUpvalue(Value *slot);
ObjString *takeString(char *chars, int length);
ObjString *allocateString(char *chars, int length, bool ownChars, uint32_t hash);
ObjBoundMethod *newBoundMethod(Value receiver, ObjClosure *method);

static bool isObjType(Value value, ObjType type)
{
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

void printObject(Value value);

#endif
