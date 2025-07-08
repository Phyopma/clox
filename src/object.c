#include <stdio.h>
#include <string.h>

#include "object.h"
#include "value.h"
#include "table.h"
#include "memory.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type *)allocateObj(sizeof(type), objectType)

Obj *allocateObj(size_t size, ObjType type)
{
    Obj *obj = (Obj *)reallocate(NULL, 0, size);
    obj->type = type;
    vm.objects = obj;
    return obj;
}

ObjString *allocateString(char *chars, int length, bool ownChars, uint32_t hash)
{
    ObjString *string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->chars = chars;
    string->length = length;
    string->hash = hash;
    string->ownChars = ownChars;
    tableSet(&vm.strings, string, NIL_VAL);

    return string;
}

static uint32_t hashString(const char *key, int length)
{
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++)
    {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

ObjString *copyString(const char *chars, int length)
{

    uint32_t hash = hashString(chars, length);

    ObjString *interned = tableFindString(&vm.strings, chars, length, hash);

    if (interned != NULL)
        return interned;

    char *heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';

    return allocateString(heapChars, length, true, hash);
}

static void printFunction(ObjFunction *function)
{
    if (function->name == NULL)
    {
        printf("<script>");
        return;
    }
    printf("<fn %s>", function->name->chars);
}

ObjString *takeString(char *chars, int length)
{
    uint32_t hash = hashString(chars, length);

    ObjString *interned = tableFindString(&vm.strings, chars, length, hash);

    if (interned != NULL)
    {
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }

    return allocateString(chars, length, true, hash);
}

ObjFunction *newFunction()
{
    ObjFunction *function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->name = NULL;
    initChunk(&function->chunk);
    return function;
}

ObjNative *newNative(NativeFn function)
{
    ObjNative *native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
    native->function = function;
    return native;
}

void printObject(Value value)
{
    switch (OBJ_TYPE(value))
    {
    case OBJ_STRING:
        printf("%s", AS_CSTRING(value));
        break;
    case OBJ_FUNCTION:
        printFunction(AS_FUNCTION(value));
        break;
    case OBJ_NATIVE:
        printf("<native fn>");
        break;
    default:
        break;
    }
}