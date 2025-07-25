#include <stdlib.h>

#include "compiler.h"
#include "memory.h"
#include "vm.h"
#include "object.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 2

void *reallocate(void *pointer, size_t oldSize, size_t newSize)
{
	vm.bytesAllocated += newSize - oldSize;
	if (newSize > oldSize)
	{
#ifdef DEBUG_STRESS_GC
		collectGarbage();
#endif
		if (vm.bytesAllocated > vm.nextGC)
		{
			collectGarbage();
		}
	}
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
void markObject(Obj *obj)
{
	if (obj == NULL)
		return;
	if (obj->isMarked)
		return;

#ifdef DEBUG_LOG_GC
	printf("%p mark", (void *)obj);
	printValue(OBJ_VAL(obj));
	printf("\n");
#endif
	obj->isMarked = true;
	if (vm.grayCapacity < vm.grayCount + 1)
	{
		vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
		vm.grayStack = (Obj **)realloc(vm.grayStack, sizeof(Obj *) * vm.grayCapacity);
		if (vm.grayStack == NULL)
			exit(1);
	}

	vm.grayStack[vm.grayCount++] = obj;
}

static void freeObject(Obj *obj)
{
#ifdef DEBUG_LOG_GC
	printf("%p free type %d\n", (void *)obj, obj->type);
#endif
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
	case OBJ_CLASS:
	{
		ObjClass *klass = (ObjClass *)obj;
		freeTable(&klass->methods);
		FREE(ObjClass, obj);
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
	case OBJ_INSTANCE:
	{
		ObjInstance *instance = (ObjInstance *)obj;
		freeTable(&instance->fields);
		FREE(ObjInstance, obj);
		break;
	}
	case OBJ_BOUND_METHOD:
	{
		FREE(ObjBoundMethod, obj);
		break;
	}
	}
}

void markValue(Value value)
{
	if (IS_OBJ(value))
		markObject(AS_OBJ(value));
}

static void markRoots()
{
	for (Value *slot = vm.stack; slot < vm.stackTop; slot++)
	{
		markValue(*slot);
	}

	for (int i = 0; i < vm.frameCount; i++)
	{
		markObject((Obj *)vm.frames[i].closure);
	}

	for (ObjUpvalue *upvalue = vm.openUpvalues; upvalue != NULL; upvalue = upvalue->next)
	{
		markObject((Obj *)upvalue);
	}

	markTable(&vm.globals);
	markCompilerRoots();
}

static void markArray(ValueArray *array)
{
	for (int i = 0; i < array->count; i++)
	{
		markValue(array->values[i]);
	}
}

static void blackenObject(Obj *obj)
{
#ifdef DEBUG_LOG_GC
	printf("%p blacken ", (void *)obj);
	printValue(OBJ_VAL(obj));
	printf("\n");
#endif
	switch (obj->type)
	{
	case OBJ_CLASS:
	{
		ObjClass *klass = (ObjClass *)obj;
		markTable(&klass->methods);
		markObject((Obj *)klass->name);
		break;
	}
	case OBJ_UPVALUE:
	{
		markValue(((ObjUpvalue *)obj)->closed);
		break;
	}
	case OBJ_FUNCTION:
	{
		ObjFunction *function = (ObjFunction *)obj;
		markObject((Obj *)function->name);
		markArray(&function->chunk.constants);
		break;
	}
	case OBJ_CLOSURE:
	{
		ObjClosure *closure = (ObjClosure *)obj;
		markObject((Obj *)closure->function);
		for (int i = 0; i < closure->upvalueCount; i++)
		{
			markObject((Obj *)closure->upvalues[i]);
		}
		break;
	}
	case OBJ_INSTANCE:
	{
		ObjInstance *instance = (ObjInstance *)obj;
		markObject((Obj *)instance->klass);
		markTable(&instance->fields);
		break;
	}
	case OBJ_BOUND_METHOD:
	{
		ObjBoundMethod *bound = (ObjBoundMethod *)obj;
		markValue(bound->receiver);
		markObject((Obj *)bound->method);
		break;
	}

	case OBJ_NATIVE:
	case OBJ_STRING:
		break;
	}
}

static void traceReferences()
{
	while (vm.grayCount > 0)
	{
		Obj *obj = vm.grayStack[--vm.grayCount];
		blackenObject(obj);
	}
}

static void sweep()
{
	Obj *prev = NULL;
	Obj *obj = vm.objects;
	while (obj != NULL)
	{
		if (obj->isMarked)
		{
			obj->isMarked = false;
			prev = obj;
			obj = obj->next;
		}
		else
		{
			Obj *unreached = obj;
			obj = obj->next;
			if (prev != NULL)
			{
				prev->next = obj;
			}
			else
			{
				vm.objects = obj;
			}

			freeObject(unreached);
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
	free(vm.grayStack);
}

void collectGarbage()
{
#ifdef DEBUG_LOG_GC
	printf("--gc begin\n");
	size_t before = vm.bytesAllocated;
#endif
	markRoots();
	traceReferences();
	tableRemoveWhite(&vm.strings);
	sweep();
	vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;
#ifdef DEBUG_LOG_GC
	printf("--gc end\n");
	printf(" collected %zu bytes (from %zu to %zu) next at %zu\n", before - vm.bytesAllocated, before, vm.bytesAllocated, vm.nextGC);
#endif
}