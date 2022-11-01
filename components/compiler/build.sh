#!/usr/bin/env bash

# Yes, I know I could just use a Makefile.  This script is for bootstrapping purposes.

CC=${CC:-gcc}
FLAGS="-O0 -g -ggdb -DDEBUG -fsanitize=address"

SOURCE_DIR=./src
OUTPUT_DIR=./bin/boot

source_files=(
    "array.c"
    "chunk.c"
    "closure.c"
    "compiler.c"
    "continuation.c"
    "core.c"
    "disasm.c"
    "fs.c"
    "function.c"
    "gc.c"
    "io.c"
    "keyword.c"
    "list.c"
    "math.c"
    "mem.c"
    "module.c"
    "native.c"
    "object.c"
    "process.c"
    "reader.c"
    "record.c"
    "repl.c"
    "scanner.c"
    "string.c"
    "symbol.c"
    "syntax.c"
    "table.c"
    "time.c"
    "value.c"
    "vm.c"
)

object_files=()

if [ ! -d $OUTPUT_DIR ]; then
    mkdir -p $OUTPUT_DIR
fi

# Build any changed source files
for i in "${source_files[@]}"
do
    input_file="$SOURCE_DIR/$i"
    output_file="$OUTPUT_DIR/${i%.c}.o"
    object_files+=("$output_file")

    if [[ $input_file -nt $output_file ]]; then
        echo "Compiling $i..."
        $CC -c $FLAGS "$SOURCE_DIR/$i" -o "$OUTPUT_DIR/${i%.c}.o"
        [ $? -eq 1 ] && exit 1
    else
        echo "Skipping $i, it is up to date."
    fi
done

# Build the static library
echo -e "Creating static library $OUTPUT_DIR/libmesche.a...\n"
ar rcs $OUTPUT_DIR/libmesche.a "${object_files[@]}"
