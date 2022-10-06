#!/usr/bin/env bash

CC=${CC:-gcc}
TEST_DIR=test
OUTPUT_DIR=bin
DEBUG_FLAGS="-O0 -g -ggdb -DDEBUG -fsanitize=address -lm"

test_files=(
    "test-main.c"
    "test-scanner.c"
    "test-reader.c"
    "test-compiler.c"
    "test-vm.c"
)

object_files=()

# Build the compiler
# LCC="$(pwd)/$CC"
LCC=gcc
CC=$LCC ./build.sh
[ $? -eq 1 ] && exit 1

# Build any changed source files
for i in "${test_files[@]}"
do
    input_file="$TEST_DIR/$i"
    output_file="$OUTPUT_DIR/${i%.c}.o"
    object_files+=("$output_file")

    if [[ $input_file -nt $output_file ]]; then
        echo "Compiling $i..."
        $CC -c $DEBUG_FLAGS "$TEST_DIR/$i" -o "$OUTPUT_DIR/${i%.c}.o"
        [ $? -eq 1 ] && exit 1
    else
        echo "Skipping $i, it is up to date."
    fi
done

# Build the static library
echo -e "\nCreating test harness bin/run-tests..."
$CC -o bin/run-tests "${object_files[@]}" bin/libmesche.a $DEBUG_FLAGS
[ $? -eq 1 ] && exit 1
chmod +x bin/run-tests
./bin/run-tests
