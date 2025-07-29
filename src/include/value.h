#ifndef clox_value_h
#define clox_value_h

#include "common.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;

#ifdef NAN_BOXING
#include <string.h>

typedef uint64_t Value;
#define QNAN ((uint64_t)0x7ffc000000000000)
#define TAG_NIL 1   // 01
#define TAG_FALSE 2 // 10
#define TAG_TRUE 3  // 11
#define SIGN_BIT (0x8000000000000000)

#define NUMBER_VAL(num) numToValue(num)
#define NIL_VAL ((Value)(u_int64_t)(QNAN | TAG_NIL))
#define FALSE_VAL ((Value)(u_int64_t)(QNAN | TAG_FALSE))
#define TRUE_VAL ((Value)(u_int64_t)(QNAN | TAG_TRUE))
#define BOOL_VAL(b) ((Value)b ? TRUE_VAL : FALSE_VAL)
#define OBJ_VAL(obj) ((Value)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)obj))

#define IS_NUMBER(val) ((val & QNAN) != QNAN)
#define IS_NIL(val) (val == NIL_VAL)
#define IS_BOOL(val) ((val | 1) == TRUE_VAL)
#define IS_OBJ(val) (((val) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

#define AS_NUMBER(val) valueToNumber(val)
#define AS_BOOL(val) (val == TRUE_VAL)
#define AS_OBJ(val) ((Obj *)(uintptr_t)(val & ~(SIGN_BIT | QNAN)))

static inline Value numToValue(double num)
{
    Value value;
    memcpy(&value, &num, sizeof(double));
    return value;
}

static inline double valueToNumber(Value value)
{
    double num;
    memcpy(&num, &value, sizeof(Value));
    return num;
}

#else
typedef enum
{
    VAL_BOOL,
    VAL_NIL,
    VAL_OBJ,
    VAL_NUMBER
} ValueType;
typedef struct
{
    ValueType type;
    union
    {
        bool boolean;
        double number;
        Obj *obj;
    } as;
} Value;

#define IS_BOOL(value) ((value).type == VAL_BOOL)
#define IS_NIL(value) ((value).type == VAL_NIL)
#define IS_OBJ(value) ((value).type == VAL_OBJ)
#define IS_NUMBER(value) ((value).type == VAL_NUMBER)

#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)
#define AS_OBJ(value) ((value).as.obj)

#define BOOL_VAL(value) ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL ((Value){VAL_NIL, {.number = 0}})
#define OBJ_VAL(object) ((Value){VAL_OBJ, {.obj = (Obj *)object}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = value}})

#endif
typedef struct
{
    int capacity;
    int count;
    Value *values;
} ValueArray;

bool valuesEqual(Value a, Value b);
void initValueArray(ValueArray *array);
void writeValueArray(ValueArray *array, Value value);
void freeValueArray(ValueArray *array);
void printValue(Value value);

#endif