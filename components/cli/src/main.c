#include <errno.h>
#include <mesche.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
  // Set up the compiler's load path relative to the program path
  char cwd[500];
  char *program_path = mesche_process_executable_path();
  char *program_dir = mesche_fs_file_directory(strdup(program_path));
  char *original_dir = getcwd(&cwd[0], sizeof(cwd));

  // Resolve the modules path with respect to the program path
  char *tmp_path = mesche_cstring_join(program_dir, strlen(program_dir),
#ifdef DEV_BUILD
                                       "/../../modules", 14,
#else
                                       "/modules", 8,
#endif
                                       NULL);

  char *modules_path = mesche_fs_resolve_path(tmp_path);
  free(tmp_path);
  tmp_path = mesche_cstring_join(modules_path, strlen(modules_path), "/main.msc", 9, NULL);
  char *main_file_path = mesche_fs_resolve_path(tmp_path);

  VM vm;
  mesche_vm_init(&vm, argc, argv);
  mesche_vm_load_path_add(&vm, modules_path);
  mesche_vm_register_core_modules(&vm, modules_path);

  // Evaluate the main entrypoint file
  mesche_vm_load_file(&vm, main_file_path);

  // Report the final memory allocation statistics
  /* mesche_mem_report((MescheMemory *)&vm); */

  // Write out a final newline for now
  printf("\n");

  // Free the VM and exit
  mesche_vm_free(&vm);

  // Free temporary strings
  free(tmp_path);
  free(main_file_path);
  free(modules_path);
  free(program_dir);
  free(program_path);

  return 0;
}
