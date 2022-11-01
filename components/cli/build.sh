#!/usr/bin/env bash

# Yes, I know I could just use a Makefile.  This script is for bootstrapping purposes.

CC="gcc"
FLAGS="-O0 -g -ggdb -fsanitize=address -lm"
BUILD_ARGS="--debug"

SOURCE_DIR=src
OUTPUT_DIR=bin/boot
CFLAGS="-I ./../compiler/include"

MESCHE_LIB="./../compiler/bin/boot/libmesche.a"

# Make sure the compiler's output library exists
if [ ! -f $MESCHE_LIB ]; then
    echo -e "Mesche compiler library not found at $MESCHE_LIB, exiting...\n"
    exit 1
fi

# Build the CLI
source_files=(
    "main.c"
)

object_files=()

if [ ! -d "$OUTPUT_DIR" ]; then
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
        $CC -c $FLAGS "$SOURCE_DIR/$i" -o "$OUTPUT_DIR/${i%.c}.o" $CFLAGS
        [ $? -eq 1 ] && exit 1
    else
        echo "Skipping $i, it is up to date."
    fi
done

# Copy module files to the output path
cp -R ./modules/ $OUTPUT_DIR

# Build the CLI program
echo -e "Creating Mesche CLI $OUTPUT_DIR/mesche...\n"
$CC -o $OUTPUT_DIR/mesche "${object_files[@]}" ./../compiler/bin/boot/libmesche.a $FLAGS
[ $? -eq 1 ] && exit 1
chmod +x $OUTPUT_DIR/mesche
