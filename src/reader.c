#include <stdio.h>
#include <stdlib.h>

#include "array.h"
#include "keyword.h"
#include "object.h"
#include "reader.h"
#include "scanner.h"
#include "symbol.h"
#include "vm-impl.h"

// The Mesche reader loosely follows the specification in R7RS section 7.1.2,
// "External representations" and adds some extra datums like keywords.

void mesche_reader_init(ReaderContext *context, struct VM *vm) { context->vm = vm; }

void mesche_reader_from_file(ReaderContext *context, Reader *reader, const char *source,
                             ObjectString *file_name) {
  reader->file_name = file_name;
  mesche_reader_from_string(context, reader, source);
}

void mesche_reader_from_string(ReaderContext *context, Reader *reader, const char *source) {
  reader->context = context;
  mesche_scanner_init(&reader->scanner, source);
}

ObjectSyntax *mesche_reader_read_next(Reader *reader) {
  ObjectCons *current_head = NULL;
  ObjectCons *current_cons = NULL;

  int list_depth = 0;
  bool is_cons_dotted = false;
  bool is_quoted = false;

#define SYNTAX(value, token)                                                                       \
  mesche_object_make_syntax(reader->context->vm, token.line, 0, 0, 0, reader->file_name, value)

  // TODO: It seems like all cons pairs created in FINISH should have their
  // syntaxes updated to include the full range of their portion of the list...

#define FINISH(value, token)                                                                       \
  /* First, check if the expression should be quoted and wrap it in a (quote ...) */               \
  if (is_quoted) {                                                                                 \
    value = OBJECT_VAL(mesche_object_make_cons(                                                    \
        reader->context->vm,                                                                       \
        OBJECT_VAL(SYNTAX(OBJECT_VAL(reader->context->vm->quote_symbol), token)),                  \
        OBJECT_VAL(SYNTAX(OBJECT_VAL(mesche_object_make_cons(                                      \
                              reader->context->vm, OBJECT_VAL(SYNTAX(value, token)), EMPTY_VAL)),  \
                          token))));                                                               \
    is_quoted = false;                                                                             \
  }                                                                                                \
  if (current_cons != NULL) {                                                                      \
    /* If current_cons is not null, that means we add the item to the current list. */             \
    if (is_cons_dotted) {                                                                          \
      /* TODO: What do we do when there are more items after a dotted cons is finished? */         \
      current_cons->cdr = OBJECT_VAL(SYNTAX(value, token));                                        \
      is_cons_dotted = false;                                                                      \
    } else {                                                                                       \
      /* Add the value to the end of the existing list. */                                         \
      ObjectCons *next_cons = mesche_object_make_cons(                                             \
          reader->context->vm, OBJECT_VAL(SYNTAX(value, token)), EMPTY_VAL);                       \
      current_cons->cdr = OBJECT_VAL(SYNTAX(OBJECT_VAL(next_cons), token));                        \
      current_cons = next_cons;                                                                    \
    }                                                                                              \
  } else if (current_head != NULL) {                                                               \
    /* A non-null current_head indicates a new list so populate car with the value. */             \
    current_head->car = OBJECT_VAL(SYNTAX(value, token));                                          \
    current_cons = current_head;                                                                   \
  } else {                                                                                         \
    /* Simply return the value wrapped in a syntax object. */                                      \
    return SYNTAX(value, token);                                                                   \
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
      // Reached the end of input
      return SYNTAX(EOF_VAL, current);
    }

    // Things left to parse:
    // - Atoms: #\char, #b11010010
    // - Vectors: #(1 2 3)

    // Which kind of token is it?
    if (current.kind == TokenKindTrue) {
      FINISH(TRUE_VAL, current);
    } else if (current.kind == TokenKindFalse) {
      FINISH(FALSE_VAL, current);
    } else if (current.kind == TokenKindNumber) {
      Value number = NUMBER_VAL(strtod(current.start, NULL));
      FINISH(number, current);
    } else if (current.kind == TokenKindString) {
      Value str = OBJECT_VAL(
          mesche_object_make_string(reader->context->vm, current.start + 1, current.length - 2));
      FINISH(str, current);
    } else if (current.kind == TokenKindKeyword) {
      Value keyword = OBJECT_VAL(
          mesche_object_make_keyword(reader->context->vm, current.start + 1, current.length - 1));
      FINISH(keyword, current);
    } else if (current.kind == TokenKindSymbol) {
      // Is the symbol a dot?
      if (current.sub_kind == TokenKindDot && current_cons) {
        // Treat this as a dotted list
        is_cons_dotted = true;
      } else {
        ObjectSymbol *symbol =
            mesche_object_make_symbol(reader->context->vm, current.start, current.length);
        symbol->token_kind = current.sub_kind;

        Value symbol_val = OBJECT_VAL(symbol);
        FINISH(symbol_val, current);
      }
    } else if (current.kind == TokenKindLeftParen) {
      // Create a new list
      if (current_head) {
        // Push the current conses to the value stack to prevent GC
        mesche_vm_stack_push(reader->context->vm, OBJECT_VAL(current_head));
        mesche_vm_stack_push(reader->context->vm, OBJECT_VAL(current_cons));
      }

      // Creating a new head and leaving current_cons empty signifies that we
      // created a new list.
      // TODO: Push this onto the stack immediately!
      current_head = mesche_object_make_cons(reader->context->vm, UNSPECIFIED_VAL, EMPTY_VAL);
      current_cons = NULL;

      // Push the quote state to the stack too so that everything inside the
      // list doesn't become explicitly quoted
      mesche_vm_stack_push(reader->context->vm, BOOL_VAL(is_quoted));
      is_quoted = false;

      // Increase the list depth
      list_depth++;
    } else if (current.kind == TokenKindRightParen) {
      // If the list was actually initialized, return it.  Otherwise,
      // return the empty list value.
      Value list = IS_UNSPECIFIED(current_head->car) ? EMPTY_VAL : OBJECT_VAL(current_head);

      // Restore the quote state
      is_quoted = AS_BOOL(mesche_vm_stack_pop(reader->context->vm));

      // Do we have remaining lists to process?
      if (--list_depth > 0) {
        // Restore the previous conses so that this list is added to them
        current_cons = AS_CONS(mesche_vm_stack_pop(reader->context->vm));
        current_head = AS_CONS(mesche_vm_stack_pop(reader->context->vm));
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
    } else if (current.kind == TokenKindEOF) {
      // Make sure we've parsed a complete form
      if (list_depth > 0) {
        // TODO: Include file and line number
        printf("Unbalanced parentheses.\n");
        return SYNTAX(UNSPECIFIED_VAL, current);
      } else {
        return SYNTAX(EMPTY_VAL, current);
      }
    }
  }
}
