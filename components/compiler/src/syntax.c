#include "mem.h"
#include "native.h"
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

static void mesche_syntax_print_value(Value value) {
  if (IS_SYNTAX(value)) {
    ObjectSyntax *syntax = AS_SYNTAX(value);
    value = syntax->value;
  }

  if (IS_CONS(value)) {
    ObjectCons *cons = AS_CONS(value);
    printf("(");
    mesche_syntax_print_value(cons->car);
    if (IS_CONS(cons->cdr) || (IS_SYNTAX(cons->cdr) && IS_CONS(AS_SYNTAX(cons->cdr)->value))) {
      printf(" ");
      mesche_syntax_print_value(cons->cdr);
    } else if (!IS_EMPTY(cons->cdr)) {
      printf(" . ");
      mesche_syntax_print_value(cons->cdr);
    }
    printf(")");
  } else {
    mesche_value_print(value);
  }
}

void mesche_syntax_print(ObjectSyntax *syntax) {
  printf("#<syntax:%d:%d ", syntax->line, syntax->column);
  mesche_syntax_print_value(syntax->value);
  printf(">");
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

Value syntax_line_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires a single parameter.");
  }

  ObjectSyntax *syntax = AS_SYNTAX(args[0]);
  return NUMBER_VAL(syntax->line);
}

Value syntax_column_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires a single parameter.");
  }

  ObjectSyntax *syntax = AS_SYNTAX(args[0]);
  return NUMBER_VAL(syntax->column);
}

Value syntax_position_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires a single parameter.");
  }

  ObjectSyntax *syntax = AS_SYNTAX(args[0]);
  return NUMBER_VAL(syntax->position);
}

Value syntax_span_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires a single parameter.");
  }

  ObjectSyntax *syntax = AS_SYNTAX(args[0]);
  return NUMBER_VAL(syntax->span);
}

Value syntax_source_msc(MescheMemory *mem, int arg_count, Value *args) {
  if (arg_count != 1) {
    PANIC("Function requires a single parameter.");
  }

  ObjectSyntax *syntax = AS_SYNTAX(args[0]);
  return OBJECT_VAL(syntax->file_name);
}

Value syntax_value_msc(MescheMemory *mem, int arg_count, Value *args) {
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
