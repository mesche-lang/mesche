#ifndef mesche_process_h
#define mesche_process_h

#include <unistd.h>

#include "vm.h"

#define IS_PROCESS(value) mesche_object_is_kind(value, ObjectKindProcess)
#define AS_PROCESS(value) ((MescheProcess *)AS_OBJECT(value))

typedef enum { PROCESS_NOT_STARTED, PROCESS_RUNNING, PROCESS_FINISHED } MescheProcessState;

typedef struct {
  Object object;
  pid_t id;
  int exit_code;
  MescheProcessState state;
  MeschePort *stdout_port;
  MeschePort *stderr_port;
} MescheProcess;

char *mesche_process_executable_path(void);
void mesche_process_module_init(VM *vm);

void mesche_process_print(MeschePort *output_port, MescheProcess *process, MeschePrintStyle style);
void mesche_free_process(VM *vm, MescheProcess *process);

#endif
