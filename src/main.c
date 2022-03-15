#include <errno.h>
#include <mesche.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
  // Set up the compiler's load path relative to the program path
  char cwd[500];
  char *program_path = mesche_fs_resolve_path(argv[0]);
  char *program_dir = mesche_fs_file_directory(strdup(program_path));
  char *original_dir = getcwd(&cwd[0], sizeof(cwd));

  // Change directory to resolve the path correctly
  chdir(program_dir);
  char *main_file_path = mesche_fs_resolve_path("./../modules/main.msc");
  char *modules_path = mesche_fs_resolve_path("./../modules/");
  chdir(original_dir);

  // Make sure the VM has the full program path
  argv[0] = program_path;

  VM vm;
  mesche_vm_init(&vm, argc, argv);
  mesche_vm_load_path_add(&vm, modules_path);
  mesche_vm_register_core_modules(&vm);

  // Evaluate the main entrypoint file
  mesche_vm_load_file(&vm, main_file_path);

  // Report the final memory allocation statistics
  /* mesche_mem_report((MescheMemory *)&vm); */

  // Write out a final newline for now
  printf("\n");

  // Free the VM and exit
  mesche_vm_free(&vm);

  // Free temporary strings
  free(main_file_path);
  free(modules_path);
  free(program_dir);
  free(program_path);

  return 0;
}
