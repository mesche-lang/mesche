#ifndef mesche_io_h
#define mesche_io_h

#include "vm.h"

// For now, a MeschePort is just a file pointer
typedef FILE MeschePort;

void mesche_io_module_init(VM *vm);

#endif
