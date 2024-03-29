#include <stdio.h>
#include <stdlib.h>

#include "array.h"
#include "keyword.h"
#include "native.h"
#include "object.h"
#include "reader.h"
#include "scanner.h"
#include "symbol.h"
#include "util.h"
#include "vm-impl.h"

// The Mesche reader loosely follows the specification in R7RS section 7.1.2,
// "External representations" and adds some extra datums like keywords.

void mesche_reader_init(Reader *reader, VM *vm, MeschePort *port, ObjectString *file_name) {
  // TODO: Pick the file name from a file port if given one
  reader->vm = vm;
  reader->file_name = file_name;
  mesche_scanner_init(&reader->scanner, vm, port);
}

Value reader_interpret_char_literal(Token token) {
  char result = 0;
  char *literal = token.start + 2;

  if (literal[0] == 'x') {
    // TODO: Implement this
    PANIC("Cannot interpret hex literal %s", literal);
  } else if (token.length > 3) {
    // Interpret the string name of the character
    // TODO: This should be a loop over a mapping table
    if (strncmp(literal, "newline", token.length - 2) == 0) {
      result = '\n';
    } else if (strncmp(literal, "return", token.length - 2) == 0) {
      result = '\r';
    } else if (strncmp(literal, "tab", token.length - 2) == 0) {
      result = '\t';
    } else if (strncmp(literal, "space", token.length - 2) == 0) {
      result = ' ';
    } else if (strncmp(literal, "null", token.length - 2) == 0) {
      result = 0;
    } else {
      // TODO: Return a syntax error
      PANIC("Cannot interpret character literal %s", literal);
    }
  } else {
    // Return the character directly
    result = literal[0];
  }

  return CHAR_VAL(result);
}

Value mesche_reader_read_next(Reader *reader) {
  ObjectCons *current_head = NULL;
  ObjectCons *current_cons = NULL;

  int list_depth = 0;
  bool is_cons_dotted = false;
  bool is_quoted = false;

#define SYNTAX(out_var, value, token)                                                              \
  mesche_vm_stack_push(reader->vm, value);                                                         \
  out_var = mesche_object_make_syntax(reader->vm, token.line, 0, 0, 0, reader->file_name, value);  \
  mesche_vm_stack_pop(reader->vm);

  // TODO: It seems like all cons pairs created in FINISH should have their
  // syntaxes updated to include the full range of their portion of the list...

#define FINISH(value, token)                                                                       \
  /* First, check if the expression should be quoted and wrap it in a (quote ...) */               \
  if (is_quoted) {                                                                                 \
    /*                                                                                             \
     *  We create a lot of intermediate objects in this function so we must push                   \
     *  them to the value stack to ensure that they don't get reclaimed by the                     \
     *  GC at an inopportune time.                                                                 \
     */                                                                                            \
    ObjectSyntax *quote_syntax, *cons_syntax, *value_syntax;                                       \
    SYNTAX(value_syntax, value, token)                                                             \
    mesche_vm_stack_push(reader->vm, OBJECT_VAL(value_syntax));                                    \
    SYNTAX(quote_syntax, OBJECT_VAL(reader->vm->quote_symbol), token)                              \
    mesche_vm_stack_push(reader->vm, OBJECT_VAL(quote_syntax));                                    \
    ObjectCons *cons = mesche_object_make_cons(reader->vm, OBJECT_VAL(value_syntax), EMPTY_VAL);   \
    mesche_vm_stack_push(reader->vm, OBJECT_VAL(quote_syntax));                                    \
    SYNTAX(cons_syntax, OBJECT_VAL(cons), token)                                                   \
    mesche_vm_stack_push(reader->vm, OBJECT_VAL(cons_syntax));                                     \
    value = OBJECT_VAL(                                                                            \
        mesche_object_make_cons(reader->vm, OBJECT_VAL(quote_syntax), OBJECT_VAL(cons_syntax)));   \
    mesche_vm_stack_pop(reader->vm);                                                               \
    mesche_vm_stack_pop(reader->vm);                                                               \
    mesche_vm_stack_pop(reader->vm);                                                               \
    mesche_vm_stack_pop(reader->vm);                                                               \
    is_quoted = false;                                                                             \
  }                                                                                                \
  if (current_cons != NULL) {                                                                      \
    /* If current_cons is not null, that means we add the item to the current list. */             \
    if (is_cons_dotted) {                                                                          \
      /* TODO: What do we do when there are more items after a dotted cons is finished? */         \
      ObjectSyntax *value_syntax;                                                                  \
      SYNTAX(value_syntax, value, token)                                                           \
      current_cons->cdr = OBJECT_VAL(value_syntax);                                                \
      is_cons_dotted = false;                                                                      \
    } else {                                                                                       \
      /* Add the value to the end of the existing list. */                                         \
      ObjectSyntax *value_syntax, *cons_syntax;                                                    \
      SYNTAX(value_syntax, value, token)                                                           \
      /* Push the value_syntax so that it doesn't get reclaimed early! */                          \
      mesche_vm_stack_push(reader->vm, OBJECT_VAL(value_syntax));                                  \
      ObjectCons *next_cons =                                                                      \
          mesche_object_make_cons(reader->vm, OBJECT_VAL(value_syntax), EMPTY_VAL);                \
      mesche_vm_stack_push(reader->vm, OBJECT_VAL(next_cons));                                     \
      SYNTAX(cons_syntax, OBJECT_VAL(next_cons), token)                                            \
      current_cons->cdr = OBJECT_VAL(cons_syntax);                                                 \
      current_cons = next_cons;                                                                    \
      mesche_vm_stack_pop(reader->vm);                                                             \
      mesche_vm_stack_pop(reader->vm);                                                             \
    }                                                                                              \
  } else if (current_head != NULL) {                                                               \
    /* A non-null current_head indicates a new list so populate car with the value. */             \
    ObjectSyntax *value_syntax;                                                                    \
    SYNTAX(value_syntax, value, token)                                                             \
    current_head->car = OBJECT_VAL(value_syntax);                                                  \
    current_cons = current_head;                                                                   \
  } else {                                                                                         \
    /* Simply return the value wrapped in a syntax object. */                                      \
    ObjectSyntax *value_syntax;                                                                    \
    SYNTAX(value_syntax, value, token)                                                             \
    return OBJECT_VAL(value_syntax);                                                               \
  }

  // Read a single form
  Token current;
  for (;;) {
    current = mesche_scanner_next_token(&reader->scanner);
    // Consume tokens until we hit a non-error token
    if (current.kind == TokenKindError) {
      // Create a parse error
      // TODO: Correct error
      printf("Reached a reader error.\n");
      break;
    } else if (current.kind == TokenKindEOF) {
      // Make sure we've parsed a complete form
      if (list_depth > 0) {
        PANIC("Unterminated list found at line %d of file %s.\n", current.line,
              reader->file_name ? reader->file_name->chars : "(unknown)");
        /* return SYNTAX(UNSPECIFIED_VAL, current); */
      } else {
        // Reached the end of input
        ObjectSyntax *value_syntax;
        SYNTAX(value_syntax, EOF_VAL, current)
        return OBJECT_VAL(value_syntax);
      }
    }

    // Things left to parse:
    // - Literals: #xFF, #b11010010
    // - Vectors: #(1 2 3)

    // Which kind of token is it?
    if (current.kind == TokenKindTrue) {
      FINISH(TRUE_VAL, current);
    } else if (current.kind == TokenKindFalse) {
      FINISH(FALSE_VAL, current);
    } else if (current.kind == TokenKindNumber) {
      Value number = NUMBER_VAL(strtod(current.start, NULL));
      FINISH(number, current);
    } else if (current.kind == TokenKindCharacter) {
      Value character = reader_interpret_char_literal(current);
      FINISH(character, current);
    } else if (current.kind == TokenKindString) {
      Value str =
          OBJECT_VAL(mesche_object_make_string(reader->vm, current.start + 1, current.length - 2));
      FINISH(str, current);
    } else if (current.kind == TokenKindKeyword) {
      Value keyword =
          OBJECT_VAL(mesche_object_make_keyword(reader->vm, current.start + 1, current.length - 1));
      FINISH(keyword, current);
    } else if (current.kind == TokenKindSymbol) {
      // Is the symbol a dot?
      if (current.sub_kind == TokenKindDot && current_cons) {
        // Treat this as a dotted list
        is_cons_dotted = true;
      } else {
        ObjectSymbol *symbol = mesche_object_make_symbol(reader->vm, current.start, current.length);
        symbol->token_kind = current.sub_kind;

        Value symbol_val = OBJECT_VAL(symbol);
        FINISH(symbol_val, current);
      }
    } else if (current.kind == TokenKindLeftParen) {
      // Create a new list
      if (current_head) {
        // Push the current conses to the value stack to prevent GC
        mesche_vm_stack_push(reader->vm, OBJECT_VAL(current_head));
        mesche_vm_stack_push(reader->vm, OBJECT_VAL(current_cons));
      }

      // Creating a new head and leaving current_cons empty signifies that we
      // created a new list.
      current_head = mesche_object_make_cons(reader->vm, UNSPECIFIED_VAL, EMPTY_VAL);
      current_cons = NULL;
      mesche_vm_stack_push(reader->vm, OBJECT_VAL(current_head));

      // Push the quote state to the stack too so that everything inside the
      // list doesn't become explicitly quoted
      mesche_vm_stack_push(reader->vm, BOOL_VAL(is_quoted));
      is_quoted = false;

      // Increase the list depth
      list_depth++;
    } else if (current.kind == TokenKindRightParen) {
      // If the list was actually initialized, return it.  Otherwise,
      // return the empty list value.
      Value list = IS_UNSPECIFIED(current_head->car) ? EMPTY_VAL : OBJECT_VAL(current_head);

      // Restore the quote state
      is_quoted = AS_BOOL(mesche_vm_stack_pop(reader->vm));

      // Pop the head of the list off the stack and let it go because we don't
      // need it right now
      mesche_vm_stack_pop(reader->vm);

      // Do we have remaining lists to process?
      if (--list_depth > 0) {
        // Restore the previous conses so that this list is added to them
        current_cons = AS_CONS(mesche_vm_stack_pop(reader->vm));
        current_head = AS_CONS(mesche_vm_stack_pop(reader->vm));
      } else {
        // Clear the conses so that we return the current list as result
        current_cons = NULL;
        current_head = NULL;
      }

      FINISH(list, current);
    } else if (current.kind == TokenKindQuoteChar) {
      // TODO: Also add
      // - quasiquote
      // - unquote-splicing

      // The next datum or expression will be quoted
      is_quoted = true;
    }
  }
}

static Value reader_read_internal(VM *vm, MeschePort *port) {
  if (port->kind != MeschePortKindInput) {
    mesche_vm_raise_error(vm, "read: Can only read from a textual input port.");
  }

  Reader reader;
  mesche_reader_init(&reader, vm, port, NULL);
  return mesche_reader_read_next(&reader);
}

Value reader_read_msc(VM *vm, int arg_count, Value *args) {
  MeschePort *port = NULL;
  EXPECT_ARG_COUNT(1);
  EXPECT_OBJECT_KIND(ObjectKindPort, 0, AS_PORT, port);

  return mesche_syntax_to_datum(vm, reader_read_internal(vm, port));
}

Value reader_read_syntax_msc(VM *vm, int arg_count, Value *args) {
  MeschePort *port = NULL;
  if (arg_count == 1) {
    EXPECT_OBJECT_KIND(ObjectKindPort, 0, AS_PORT, port);
  } else {
    port = vm->input_port;
  }

  return reader_read_internal(vm, port);
}

void mesche_reader_module_init(VM *vm) {
  mesche_vm_define_native_funcs(
      vm, "mesche reader",
      (MescheNativeFuncDetails[]){{"read", reader_read_msc, true},
                                  {"read-syntax", reader_read_syntax_msc, true},
                                  {NULL, NULL, false}});
}
