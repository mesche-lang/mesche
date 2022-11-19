#include <errno.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fs.h"
#include "io.h"
#include "keyword.h"
#include "native.h"
#include "object.h"
#include "process.h"
#include "util.h"
#include "vm-impl.h"

void process_init(MescheProcess *process) {
  process->id = 0;
  process->state = PROCESS_NOT_STARTED;
  process->exit_code = -1;
  process->stdout_port = NULL;
  process->stderr_port = NULL;
}

#define PIPE_FD 0
#define PIPE_INHERIT 1

MescheProcess *mesche_process_start(VM *vm, char *program_path, char *const argv[],
                                    char pipe_config[3]) {
  pid_t child_pid;

  // Flush output streams before forking
  fflush(stdout);
  fflush(stderr);

  // Create a pipe for the child process' input and output streams
  int stdout_fd[2], stderr_fd[2];
  pipe(stdout_fd);
  pipe(stderr_fd);

  if ((child_pid = fork()) == -1) {
    // TODO: Runtime error instead
    PANIC("Could not launch subprocess for program: %s\n", program_path);
    return NULL;
  }

  MescheProcess *process = ALLOC_OBJECT(vm, MescheProcess, ObjectKindProcess);
  process_init(process);
  process->id = child_pid;
  process->state = PROCESS_RUNNING;

  // We're inside the child process
  if (child_pid == 0) {
    // Set up stdout
    close(stdout_fd[0]);
    if (pipe_config[STDOUT_FILENO] != PIPE_INHERIT) {
      dup2(stdout_fd[1], STDOUT_FILENO);
    }

    // Set up stderr
    close(stderr_fd[0]);
    if (pipe_config[STDERR_FILENO] != PIPE_INHERIT) {
      dup2(stderr_fd[1], STDERR_FILENO);
    }

    // Needed so negative PIDs can kill children of /bin/sh
    setpgid(child_pid, child_pid);

    // Execute the program
    execvp(program_path, argv);
  } else {
    // Add the process before we start allocating ports so that we avoid GC
    // sweeps
    mesche_vm_stack_push(vm, OBJECT_VAL(process));

    // Set up a file pointer for reading child process stdout
    close(stdout_fd[1]);
    if (pipe_config[STDOUT_FILENO] == PIPE_FD) {
      FILE *fp = fdopen(stdout_fd[0], "r");
      process->stdout_port =
          AS_PORT(mesche_io_make_file_port(vm, MeschePortKindInput, fp, "stdout", 0));
    }

    // Set up a file pointer for reading child process stderr
    close(stderr_fd[1]);
    if (pipe_config[STDERR_FILENO] == PIPE_FD) {
      FILE *fp = fdopen(stderr_fd[0], "r");
      process->stderr_port =
          AS_PORT(mesche_io_make_file_port(vm, MeschePortKindInput, fp, "stderr", 0));
    }

    // Remove the process from the stack
    mesche_vm_stack_pop(vm);

    return process;
  }
}

int mesche_process_result(MescheProcess *process) {
  int status = -1;

  while (waitpid(process->id, &status, 0) == -1) {
    if (errno != EINTR) {
      status = -1;
      break;
    }
  }

  // Print any exceptional exit status
  // TODO: Handle these in a more robust way
  if (WIFSIGNALED(status)) {
    printf("Child process TERMINATED by signal %d (%s)\n", WTERMSIG(status),
           strsignal(WTERMSIG(status)));
  } else if (WIFSTOPPED(status)) {
    printf("Child process STOPPED by signal %d (%s)\n", WSTOPSIG(status),
           strsignal(WSTOPSIG(status)));
  }

  process->state = PROCESS_FINISHED;
  process->exit_code = WEXITSTATUS(status);

  return WEXITSTATUS(status);
}

void mesche_process_print(MeschePort *output_port, MescheProcess *process, MeschePrintStyle style) {
  fputs("#<process ", output_port->data.file.fp);

  typedef enum { PROCESS_NOT_STARTED, PROCESS_RUNNING, PROCESS_FINISHED } MescheProcessState;
  switch (process->state) {
  case PROCESS_NOT_STARTED:
    fputs("not started", output_port->data.file.fp);
    break;
  case PROCESS_RUNNING:
    fputs("running", output_port->data.file.fp);
    break;
  case PROCESS_FINISHED:
    fprintf(output_port->data.file.fp, "finished, code: %d", process->exit_code);
    break;
  }

  fputs(">", output_port->data.file.fp);
}

void mesche_free_process(VM *vm, MescheProcess *process) {
  // Release handles to ports, they will clean themselves up once freed
  process->stdout_port = NULL;
  process->stderr_port = NULL;

  FREE(vm, MescheProcess, process);
}

char *mesche_process_executable_path(void) {
  char *proc_path = "/proc/self/exe";
  char exec_path[1024];

  // TODO: Add OS-specific implementations for Windows and macOS!
  if (mesche_fs_path_exists_p(proc_path)) {
    // TODO: What happens if /proc/self/exe does not exist?
    readlink(proc_path, exec_path, sizeof(exec_path));
    return strdup(exec_path);
  } else {
    PANIC("On Linux, the path %s unexpectedly does not exist!\n", proc_path);
  }
}

Value process_arguments_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 0) {
    PANIC("Function does not accept arguments.");
  }

  Value first = EMPTY_VAL;
  for (int i = vm->arg_count - 1; i >= 0; i--) {
    ObjectString *arg_string =
        mesche_object_make_string(vm, vm->arg_array[i], strlen(vm->arg_array[i]));
    mesche_vm_stack_push(vm, OBJECT_VAL(arg_string));

    // Cons with the previous argument
    first = OBJECT_VAL(mesche_object_make_cons(vm, OBJECT_VAL(arg_string), first));

    // Wait until after we use the previous cons before we pop it
    if (i < vm->arg_count - 1) {
      mesche_vm_stack_pop(vm);
    }

    // Ensure that the cons and string don't get collected immediately
    mesche_vm_stack_pop(vm);
    mesche_vm_stack_push(vm, first);
  }

  // Pop the list from the stack
  mesche_vm_stack_pop(vm);

  return first;
}

Value process_directory_msc(VM *vm, int arg_count, Value *args) {
  char cwd[512];
  char *current_path = getcwd(cwd, sizeof(cwd));
  return OBJECT_VAL(mesche_object_make_string(vm, current_path, strlen(current_path)));
}

Value process_directory_set_msc(VM *vm, int arg_count, Value *args) {
  return chdir(AS_CSTRING(args[0])) == 0 ? TRUE_VAL : FALSE_VAL;
}

MescheProcess *process_start_inner(VM *vm, int arg_count, Value *args) {
  // char **argv = malloc(sizeof(char *) * (arg_count + 1));
  char *argv[100];

  int i = 0;
  char *command_str = strdup(AS_CSTRING(args[0]));
  char *arg = strtok(command_str, " ");

  /* printf("PROCESS START:\n"); */

  do {
    // TODO: Remove this filthy hack!
    int j = 0;
    while (arg[j] != '\0') {
      if (arg[j] == '\r')
        arg[j] = '\0';
      if (arg[j] == '\n')
        arg[j] = '\0';
      j++;
    }

    /* printf("ARG: =%s=\n", arg); */

    // Don't add the arg string if it's empty (like if there were 2 spaces)
    if (strlen(arg) > 0) {
      argv[i++] = arg;
    }

    // Find the next token
    arg = strtok(NULL, " ");
  } while (arg != NULL);

  // for (int i = 0; i < arg_count; i++) {
  //   // TODO: Verify type
  //   // TODO: Guard against #f arg value
  //   argv[i] = AS_CSTRING(args[i]);
  //   printf("%s ", argv[i]);
  // }

  // The last argument must be null so that execvp knows when to stop
  argv[i] = NULL;

  // Configure pipe settings
  int keyword_start = 1; // TODO: Update this once real args are handled
  char pipe_config[3] = {PIPE_FD, PIPE_FD, PIPE_FD};
  for (int i = keyword_start; i < arg_count;) {
    if (IS_KEYWORD(args[i])) {
      // Which keyword argument is it?
      ObjectKeyword *keyword = AS_KEYWORD(args[i]);
      int stream_index = -1;
      if (memcmp(keyword->string.chars, "stdin", 5) == 0) {
        stream_index = 0;
      } else if (memcmp(keyword->string.chars, "stdout", 6) == 0) {
        stream_index = 1;
      } else if (memcmp(keyword->string.chars, "stderr", 6) == 0) {
        stream_index = 2;
      }

      // Now read up the symbol to configure the stream
      if (IS_SYMBOL(args[++i])) {
        ObjectSymbol *value = AS_SYMBOL(args[i]);
        if (memcmp(value->name->chars, "pipe", 4) == 0) {
          pipe_config[stream_index] = PIPE_FD;
        } else if (memcmp(value->name->chars, "inherit", 7) == 0) {
          pipe_config[stream_index] = PIPE_INHERIT;
        }

        i++;
      } else {
        // TODO: Runtime error
        PANIC("Unexpected value for `process-start` keyword argument.\n");
      }
    } else {
      // TODO: Runtime error
      PANIC("Unexpected argument to `process-start`.\n");
    }
  }

  MescheProcess *process = mesche_process_start(vm, argv[0], argv, pipe_config);
  free(command_str);

  return process;
}

Value process_start_msc(VM *vm, int arg_count, Value *args) {
  MescheProcess *process = process_start_inner(vm, arg_count, args);
  return OBJECT_VAL(process);
}

Value process_start_sync_msc(VM *vm, int arg_count, Value *args) {
  MescheProcess *process = process_start_inner(vm, arg_count, args);
  int exit_code = mesche_process_result(process);
  return OBJECT_VAL(process);
}

Value process_exit_code_msc(VM *vm, int arg_count, Value *args) {
  MescheProcess *process = (MescheProcess *)AS_PROCESS(args[0]);
  return NUMBER_VAL(process->exit_code);
}

Value process_stdout_msc(VM *vm, int arg_count, Value *args) {
  MescheProcess *process = (MescheProcess *)AS_PROCESS(args[0]);
  if (process->stdout_port) {
    return OBJECT_VAL(process->stdout_port);
  }

  return FALSE_VAL;
}

Value process_stderr_msc(VM *vm, int arg_count, Value *args) {
  MescheProcess *process = (MescheProcess *)AS_PROCESS(args[0]);
  if (process->stderr_port) {
    return OBJECT_VAL(process->stderr_port);
  }

  return FALSE_VAL;
}

void mesche_process_module_init(VM *vm) {
  mesche_vm_define_native_funcs(
      vm, "mesche process",
      (MescheNativeFuncDetails[]){{"process-start", process_start_msc, true},
                                  {"process-start-sync", process_start_sync_msc, true},
                                  {"process-exit-code", process_exit_code_msc, true},
                                  {"process-stdout", process_stdout_msc, true},
                                  {"process-stderr", process_stderr_msc, true},
                                  {"process-arguments", process_arguments_msc, true},
                                  {"process-directory", process_directory_msc, true},
                                  {"process-directory-set!", process_directory_set_msc, true},
                                  {NULL, NULL, false}});
}
