#ifndef mesche_gc_h
#define mesche_gc_h

#include "object.h"
#include "vm.h"

typedef void (*ObjectFreePtr)(MescheMemory *mem, void *object);
typedef void (*ObjectMarkFuncPtr)(MescheMemory *mem, Object *object);

void mesche_gc_mark_object(VM *vm, Object *object);
void mesche_gc_collect_garbage(MescheMemory *mem);

#endif
