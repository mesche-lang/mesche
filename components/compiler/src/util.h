#ifndef mesche_util_h
#define mesche_util_h

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#define PANIC(message, ...)                                                                        \
  fprintf(stderr, "\n\e[1;31mPANIC\e[0m in \e[0;93m%s\e[0m at line %d: ", __func__, __LINE__);     \
  fprintf(stderr, message, ##__VA_ARGS__);                                                         \
  exit(1);

#endif
