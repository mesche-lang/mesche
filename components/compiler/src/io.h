#ifndef mesche_io_h
#define mesche_io_h

#include <stdbool.h>
#include <stdio.h>

typedef struct MeschePort MeschePort;

typedef enum { PrintStyleData, PrintStyleOutput } MeschePrintStyle;
typedef enum { MeschePortKindInput, MeschePortKindOutput } MeschePortKind;

void mesche_io_port_print(MeschePort *output_port, MeschePort *port, MeschePrintStyle style);

#endif
