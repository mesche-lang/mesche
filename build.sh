#!/usr/bin/env bash

# Yes, I know I could just use a Makefile.  This script is for bootstrapping purposes.

CC=${CC:-gcc}
if [ "$1" == "--debug" ]; then
 FLAGS="-O0 -g -ggdb -DDEBUG -fsanitize=address"
else
 FLAGS="-O0 -g -ggdb -fPIE -lm"
fi

SOURCE_DIR=src
OUTPUT_DIR=bin

source_files=(
    "chunk.c"
    "compiler.c"
    "disasm.c"
    "io.c"
    "fs.c"
    "list.c"
    "array.c"
    "mem.c"
    "math.c"
    "module.c"
    "object.c"
    "process.c"
    "repl.c"
    "scanner.c"
    "string.c"
    "table.c"
    "time.c"
    "value.c"
    "vm.c"
)

object_files=()

if [ ! -d "./bin" ]; then
    mkdir ./bin
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
echo -e "Creating static library bin/libmesche.a...\n"
ar rcs bin/libmesche.a "${object_files[@]}"
