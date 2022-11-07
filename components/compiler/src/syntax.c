#include <stdio.h>

#include "mem.h"
#include "native.h"
#include "port.h"
#include "syntax.h"
#include "util.h"
#include "vm.h"

ObjectSyntax *mesche_object_make_syntax(VM *vm, uint32_t line, uint32_t column, uint32_t position,
                                        uint32_t span, ObjectString *file_name, Value value) {
  ObjectSyntax *syntax = ALLOC_OBJECT(vm, ObjectSyntax, ObjectKindSyntax);
  syntax->line = line;
  syntax->column = column;
  syntax->position = position;
  syntax->span = span;
  syntax->file_name = file_name;
  syntax->value = value;

  return syntax;
}

void mesche_free_syntax(VM *vm, ObjectSyntax *syntax) { FREE(vm, ObjectSyntax, syntax); }

static void mesche_syntax_print_value(MeschePort *port, Value value, MeschePrintStyle style) {
  if (IS_SYNTAX(value)) {
    ObjectSyntax *syntax = AS_SYNTAX(value);
    value = syntax->value;
  }

  if (IS_CONS(value)) {
    ObjectCons *cons = AS_CONS(value);
    fputs("(", port->fp);
    mesche_syntax_print_value(port, cons->car, style);
    if (IS_CONS(cons->cdr) || (IS_SYNTAX(cons->cdr) && IS_CONS(AS_SYNTAX(cons->cdr)->value))) {
      fputs(" ", port->fp);
      mesche_syntax_print_value(port, cons->cdr, style);
    } else if (!IS_EMPTY(cons->cdr)) {
      fputs(" . ", port->fp);
      mesche_syntax_print_value(port, cons->cdr, style);
    }
    fputs(")", port->fp);
  } else {
    mesche_value_print_ex(port, value, style);
  }
}

void mesche_syntax_print(MeschePort *port, ObjectSyntax *syntax) {
  mesche_syntax_print_ex(port, syntax, PrintStyleOutput);
}

void mesche_syntax_print_ex(MeschePort *port, ObjectSyntax *syntax, MeschePrintStyle style) {
  fprintf(port->fp, "#<syntax:%d:%d ", syntax->line, syntax->column);
  mesche_syntax_print_value(port, syntax->value, style);
  fputs(">", port->fp);
}

Value mesche_syntax_to_datum(VM *vm, Value syntax) {
  if (IS_SYNTAX(syntax)) {
    if (IS_CONS(AS_SYNTAX(syntax)->value)) {
      ObjectCons *cons = AS_CONS(AS_SYNTAX(syntax)->value);

      // Convert the car and cdr syntaxes and store them on the value stack
      // temporarily to avoid being claimed by the GC
      Value left = mesche_syntax_to_datum(vm, cons->car);
      mesche_vm_stack_push(vm, left);
      Value right = mesche_syntax_to_datum(vm, cons->cdr);
      mesche_vm_stack_push(vm, right);

      // Create the result cons
      Value result = OBJECT_VAL(mesche_object_make_cons(vm, left, right));

      // Pop the intermediate values from the stack
      mesche_vm_stack_pop(vm);
      mesche_vm_stack_pop(vm);

      return result;
    } else {
      return AS_SYNTAX(syntax)->value;
    }
  } else {
    return syntax;
  }
}

Value syntax_line_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires a single parameter.");
  }

  ObjectSyntax *syntax = AS_SYNTAX(args[0]);
  return NUMBER_VAL(syntax->line);
}

Value syntax_column_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires a single parameter.");
  }

  ObjectSyntax *syntax = AS_SYNTAX(args[0]);
  return NUMBER_VAL(syntax->column);
}

Value syntax_position_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires a single parameter.");
  }

  ObjectSyntax *syntax = AS_SYNTAX(args[0]);
  return NUMBER_VAL(syntax->position);
}

Value syntax_span_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires a single parameter.");
  }

  ObjectSyntax *syntax = AS_SYNTAX(args[0]);
  return NUMBER_VAL(syntax->span);
}

Value syntax_source_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires a single parameter.");
  }

  ObjectSyntax *syntax = AS_SYNTAX(args[0]);
  return OBJECT_VAL(syntax->file_name);
}

Value syntax_value_msc(VM *vm, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires a single parameter.");
  }

  ObjectSyntax *syntax = AS_SYNTAX(args[0]);
  return syntax->value;
}

void mesche_syntax_module_init(VM *vm) {
  mesche_vm_define_native_funcs(
      vm, "mesche core",
      (MescheNativeFuncDetails[]){{"syntax-line", syntax_line_msc, true},
                                  {"syntax-column", syntax_column_msc, true},
                                  {"syntax-position", syntax_position_msc, true},
                                  {"syntax-span", syntax_span_msc, true},
                                  {"syntax-value", syntax_value_msc, true},
                                  {"syntax-source", syntax_source_msc, true},
                                  {NULL, NULL, false}});
}
