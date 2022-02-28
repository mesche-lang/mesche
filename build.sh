#!/usr/bin/env bash

# Yes, I know I could just use a Makefile.  This script is for bootstrapping purposes.

CC=${CC:-gcc}
SOURCE_DIR=src
OUTPUT_DIR=bin
DEBUG_FLAGS="-O0 -g -ggdb -DDEBUG -fsanitize=address"

source_files=(
    "chunk.c"
    "compiler.c"
    "disasm.c"
    "fs.c"
    "list.c"
    "mem.c"
    "module.c"
    "object.c"
    "process.c"
    "repl.c"
    "scanner.c"
    "string.c"
    "table.c"
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
        $CC -c $DEBUG_FLAGS "$SOURCE_DIR/$i" -o "$OUTPUT_DIR/${i%.c}.o"
        [ $? -eq 1 ] && exit 1
    else
        echo "Skipping $i, it is up to date."
    fi
done

# Build the static library
echo -e "\nCreating static library bin/libmesche.a..."
ar rcs bin/libmesche.a "${object_files[@]}"

echo -e "\nDone!\n"
