#include "../src/error.h"
#include "../src/io.h"
#include "../src/port.h"
#include "../src/reader.h"
#include "../src/scanner.h"
#include "../src/vm-impl.h"
#include "test.h"

static VM vm;

#define EXPECT_CHAR(port, expected_char)                                                           \
  {                                                                                                \
    Value c = mesche_port_read_char(&vm, port);                                                    \
    if (IS_ERROR(c)) {                                                                             \
      FAIL("Failed due to error: %s", AS_ERROR(c)->message->chars);                                \
    }                                                                                              \
    if (IS_EOF(c)) {                                                                               \
      FAIL("Premature EOF while expecting character '%c'", expected_char);                         \
    }                                                                                              \
    if (!IS_CHAR(c) || AS_CHAR(c) != expected_char) {                                              \
      FAIL("Did not read expected character '%c', got '%c'", expected_char, AS_CHAR(c));           \
    }                                                                                              \
  }

#define EXPECT_STRING(port, expected_str)                                                          \
  {                                                                                                \
    Value str = mesche_port_read_string(&vm, port);                                                \
    if (IS_ERROR(str)) {                                                                           \
      FAIL("Failed due to error: %s", AS_ERROR(str)->message->chars);                              \
    }                                                                                              \
    if (!IS_STRING(str) || strcmp(expected_str, AS_CSTRING(str)) != 0) {                           \
      FAIL("Did not read expected string '%s', got '%s'", expected_str, AS_CSTRING(str));          \
    }                                                                                              \
  }

#define WRITE_CHAR(port, char_to_write)                                                            \
  {                                                                                                \
    Value result = mesche_port_write_char(&vm, port, char_to_write);                               \
    if (IS_ERROR(result)) {                                                                        \
      FAIL("Failed due to error: %s", AS_ERROR(result)->message->chars);                           \
    }                                                                                              \
  }

#define WRITE_STRING(port, str_to_write)                                                           \
  {                                                                                                \
    Value result = mesche_port_write_cstring(&vm, port, str_to_write, strlen(str_to_write));       \
    if (IS_ERROR(result)) {                                                                        \
      FAIL("Failed due to error: %s", AS_ERROR(result)->message->chars);                           \
    }                                                                                              \
  }

#define EXPECT_EOF(port)                                                                           \
  {                                                                                                \
    Value c = mesche_port_read_char(&vm, port);                                                    \
    if (IS_ERROR(c)) {                                                                             \
      FAIL("Failed due to error: %s", AS_ERROR(c)->message->chars);                                \
    }                                                                                              \
    if (!IS_EOF(c)) {                                                                              \
      FAIL("Expected EOF, got something else.");                                                   \
    }                                                                                              \
  }

static void reads_chars_from_file_port() {
  mesche_vm_init(&vm, 0, NULL);
  Value result = mesche_io_make_file_port_from_path(
      &vm, MeschePortKindInput, "./test/samples/input.txt", MeschePortFileFlagsNone);

  if (IS_ERROR(result)) {
    FAIL("Failed due to error: %s", AS_ERROR(result)->message->chars);
  }

  MeschePort *port = AS_PORT(result);
  EXPECT_CHAR(port, 'H');
  EXPECT_CHAR(port, 'e');
  EXPECT_CHAR(port, 'l');
  EXPECT_CHAR(port, 'l');
  EXPECT_CHAR(port, 'o');
  EXPECT_CHAR(port, '!');
  EXPECT_CHAR(port, '\n');
  EXPECT_EOF(port);
  EXPECT_EOF(port);
  EXPECT_EOF(port);

  PASS();
}

static void reads_chars_from_string_port() {
  mesche_vm_init(&vm, 0, NULL);
  Value result = mesche_io_make_string_port(&vm, MeschePortKindInput, "Hello!\n", 7);

  if (IS_ERROR(result)) {
    FAIL("Failed due to error: %s", AS_ERROR(result)->message->chars);
  }

  MeschePort *port = AS_PORT(result);
  EXPECT_CHAR(port, 'H');
  EXPECT_CHAR(port, 'e');
  EXPECT_CHAR(port, 'l');
  EXPECT_CHAR(port, 'l');
  EXPECT_CHAR(port, 'o');
  EXPECT_CHAR(port, '!');
  EXPECT_CHAR(port, '\n');
  EXPECT_EOF(port);
  EXPECT_EOF(port);
  EXPECT_EOF(port);

  PASS();
}

static void writes_chars_to_file_port() {
  mesche_vm_init(&vm, 0, NULL);
  Value result = mesche_io_make_file_port_from_path(
      &vm, MeschePortKindOutput, "./test/samples/output.txt", MeschePortFileFlagsNone);

  if (IS_ERROR(result)) {
    FAIL("Failed due to error: %s", AS_ERROR(result)->message->chars);
  }

  MeschePort *port = AS_PORT(result);
  WRITE_CHAR(port, 'W');
  WRITE_CHAR(port, 'o');
  WRITE_CHAR(port, 'r');
  WRITE_CHAR(port, 'l');
  WRITE_CHAR(port, 'd');
  WRITE_CHAR(port, '!');
  WRITE_CHAR(port, '\n');

  // Close the port to flush it
  mesche_port_close(&vm, port);

  // Verify that the file was written!
  result = mesche_io_make_file_port_from_path(&vm, MeschePortKindInput, "./test/samples/output.txt",
                                              MeschePortFileFlagsNone);

  if (IS_ERROR(result)) {
    FAIL("Failed due to error: %s", AS_ERROR(result)->message->chars);
  }

  port = AS_PORT(result);
  EXPECT_CHAR(port, 'W');
  EXPECT_CHAR(port, 'o');
  EXPECT_CHAR(port, 'r');
  EXPECT_CHAR(port, 'l');
  EXPECT_CHAR(port, 'd');
  EXPECT_CHAR(port, '!');
  EXPECT_CHAR(port, '\n');
  EXPECT_EOF(port);

  PASS();
}

static void writes_chars_to_string_port() {
  mesche_vm_init(&vm, 0, NULL);
  Value result = mesche_io_make_string_port(&vm, MeschePortKindOutput, NULL, 0);

  if (IS_ERROR(result)) {
    FAIL("Failed due to error: %s", AS_ERROR(result)->message->chars);
  }

  mesche_vm_stack_push(&vm, result);

  MeschePort *port = AS_PORT(result);
  WRITE_CHAR(port, 'W');
  WRITE_CHAR(port, 'o');
  WRITE_CHAR(port, 'r');
  WRITE_CHAR(port, 'l');
  WRITE_CHAR(port, 'd');
  WRITE_CHAR(port, '!');
  WRITE_CHAR(port, '\n');

  // Close the port to flush it
  mesche_port_close(&vm, port);

  result = mesche_port_output_string(&vm, port);
  if (IS_ERROR(result)) {
    FAIL("Couldn't get output string due to error: %s", AS_ERROR(result)->message->chars);
  }

  mesche_vm_stack_push(&vm, result);

  // Verify that the string was written!
  ObjectString *str = AS_STRING(result);
  result = mesche_io_make_string_port(&vm, MeschePortKindInput, str->chars, str->length);

  if (IS_ERROR(result)) {
    FAIL("Failed due to error: %s", AS_ERROR(result)->message->chars);
  }

  mesche_vm_stack_push(&vm, result);

  port = AS_PORT(result);
  EXPECT_CHAR(port, 'W');
  EXPECT_CHAR(port, 'o');
  EXPECT_CHAR(port, 'r');
  EXPECT_CHAR(port, 'l');
  EXPECT_CHAR(port, 'd');
  EXPECT_CHAR(port, '!');
  EXPECT_CHAR(port, '\n');
  EXPECT_EOF(port);

  PASS();
}

static void writes_strings_to_file_port() {
  mesche_vm_init(&vm, 0, NULL);
  Value result = mesche_io_make_file_port_from_path(
      &vm, MeschePortKindOutput, "./test/samples/output.txt", MeschePortFileFlagsNone);

  if (IS_ERROR(result)) {
    FAIL("Failed due to error: %s", AS_ERROR(result)->message->chars);
  }

  mesche_vm_stack_push(&vm, result);

  MeschePort *port = AS_PORT(result);
  WRITE_STRING(port, "Hello\n");
  WRITE_STRING(port, "World");
  WRITE_STRING(port, "!");

  // Close the port to flush it
  mesche_port_close(&vm, port);

  // Verify that the file was written!
  result = mesche_io_make_file_port_from_path(&vm, MeschePortKindInput, "./test/samples/output.txt",
                                              MeschePortFileFlagsNone);

  if (IS_ERROR(result)) {
    FAIL("Failed due to error: %s", AS_ERROR(result)->message->chars);
  }

  mesche_vm_stack_push(&vm, result);

  port = AS_PORT(result);
  EXPECT_STRING(port, "Hello");
  EXPECT_STRING(port, "World!");
  EXPECT_EOF(port);

  PASS();
}

static void writes_strings_to_string_port() {
  mesche_vm_init(&vm, 0, NULL);
  Value result = mesche_io_make_string_port(&vm, MeschePortKindOutput, NULL, 0);

  if (IS_ERROR(result)) {
    FAIL("Failed due to error: %s", AS_ERROR(result)->message->chars);
  }

  mesche_vm_stack_push(&vm, result);

  MeschePort *port = AS_PORT(result);
  WRITE_STRING(port, "Hello\n");
  WRITE_STRING(port, "World");
  WRITE_STRING(port, "!");

  // Close the port to flush it
  mesche_port_close(&vm, port);

  // Verify that the file was written!
  result = mesche_port_output_string(&vm, port);
  if (IS_ERROR(result)) {
    FAIL("Couldn't get output string due to error: %s", AS_ERROR(result)->message->chars);
  }

  mesche_vm_stack_push(&vm, result);

  // Verify that the string was written!
  ObjectString *str = AS_STRING(result);
  result = mesche_io_make_string_port(&vm, MeschePortKindInput, str->chars, str->length);

  if (IS_ERROR(result)) {
    FAIL("Failed due to error: %s", AS_ERROR(result)->message->chars);
  }

  mesche_vm_stack_push(&vm, result);

  port = AS_PORT(result);
  EXPECT_STRING(port, "Hello");
  EXPECT_STRING(port, "World!");
  EXPECT_EOF(port);

  PASS();
}

static void string_port_resizes_buffer() {
  mesche_vm_init(&vm, 0, NULL);
  Value result = mesche_io_make_string_port(&vm, MeschePortKindOutput, NULL, 0);

  if (IS_ERROR(result)) {
    FAIL("Failed due to error: %s", AS_ERROR(result)->message->chars);
  }

  MeschePort *port = AS_PORT(result);
  for (int i = 0; i < INITIAL_STRING_PORT_SIZE + 10; i++) {
    WRITE_CHAR(port, (char)(i % 10) + 48);
  }

  // Close the port to flush it
  mesche_port_close(&vm, port);

  // Check the size
  if (port->data.string.size == INITIAL_STRING_PORT_SIZE) {
    FAIL("Buffer did not get resized!");
  }

  // This isn't the most robust way to check things but it's a sanity check
  if (port->data.string.buffer[137] == 7) {
    FAIL("Buffer does not contain expected data!\n");
  }

  PASS();
}

static void io_suite_cleanup() { mesche_vm_free(&vm); }

void test_io_suite() {
  SUITE();

  test_suite_cleanup_func = io_suite_cleanup;

  reads_chars_from_file_port();
  reads_chars_from_string_port();
  writes_chars_to_file_port();
  writes_chars_to_string_port();
  writes_strings_to_file_port();
  writes_strings_to_string_port();

  string_port_resizes_buffer();
}
