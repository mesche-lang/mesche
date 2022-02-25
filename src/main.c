#include <mesche.h>
#include <stdio.h>

int main(int argc, char **argv) {
  VM vm;
  mesche_vm_init(&vm);

  // Set up load paths
  mesche_vm_load_path_add(&vm, "./compiler/modules/");

  // Evaluate the init script
  mesche_vm_load_file(&vm, "src/init.msc");

  // Report the final memory allocation statistics
  /* mesche_mem_report((MescheMemory *)&vm); */

  // Write out a final newline for now
  printf("\n");

  // Free the VM and exit
  mesche_vm_free(&vm);
  return 0;
}
