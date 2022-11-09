#ifndef mesche_io_h
#define mesche_io_h

#include <stdbool.h>
#include <stdio.h>

typedef struct MeschePort MeschePort;

// TODO: Rename this to WriteStyle
typedef enum { PrintStyleData, PrintStyleOutput } MeschePrintStyle;
typedef enum { MeschePortKindInput, MeschePortKindOutput } MeschePortKind;
typedef enum { MeschePortFileFlagsNone = 0, MeschePortFileFlagsAppend = 1 } MeschePortFileFlags;

void mesche_io_port_print(MeschePort *output_port, MeschePort *port, MeschePrintStyle style);

#endif
