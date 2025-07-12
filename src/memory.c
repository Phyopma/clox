#include <stdlib.h>
#include "memory.h"
#include "object.h"

void *reallocate(void *pointer, size_t oldSize, size_t newSize)
{
	if (newSize == 0)
	{
		free(pointer);
		return NULL;
	}
	void *result = realloc(pointer, newSize);
	if (result == NULL)
		exit(1);
	return result;
}
static void freeObject(Obj *obj)
{
	switch (obj->type)
	{
	case OBJ_STRING:
	{
		ObjString *string = (ObjString *)obj;
		if (string->ownChars)
			FREE_ARRAY(char, string->chars, string->length + 1);
		FREE(ObjString, obj);
		break;
	}
	case OBJ_FUNCTION:
	{
		ObjFunction *function = (ObjFunction *)obj;
		freeChunk(&function->chunk);
		FREE(ObjFunction, obj);
		break;
	}
	case OBJ_NATIVE:
	{
		FREE(ObjNative, obj);
		break;
	}
	case OBJ_CLOSURE:
	{
		ObjClosure *closure = (ObjClosure *)obj;
		FREE_ARRAY(ObjUpvalue *, closure->upvalues, closure->upvalueCount);
		FREE(ObjClosure, obj);
		break;
	}
	case OBJ_UPVALUE:
	{
		FREE(ObjUpvalue, obj);
		break;
	}
	}
}

void freeObjects()
{
	Obj *obj = vm.objects;
	while (obj != NULL)
	{
		Obj *next = obj->next;
		freeObject(obj);
		obj = next;
	}
}
