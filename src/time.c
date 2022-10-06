#include <sys/time.h>

#include "native.h"
#include "time.h"
#include "util.h"

Value time_current_msec_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (arg_count != 0) {
    PANIC("Function accepts no parameters.");
  }

  struct timeval tv;
  gettimeofday(&tv, NULL);

  return NUMBER_VAL((((long long)tv.tv_sec) * 1000) + (tv.tv_usec / 1000));
}

void mesche_time_module_init(VM *vm) {
  mesche_vm_define_native_funcs(
      vm, "mesche time",
      (MescheNativeFuncDetails[]){{"time-current-msec", time_current_msec_msc, true},
                                  {NULL, NULL, false}});
}
