#!/bin/sh

CC=gcc
SOURCE_DIR=src
OUTPUT_DIR=bin
CFLAGS="-I ./compiler/include"

# Yes, I know I could just use a Makefile.  This script is for bootstrapping purposes.

# Ensure that the compiler library is cloned
if [ ! -d "./compiler" ]; then
  echo -e "Cloning Mesche Compiler repo...\n"
  # Control will enter here if $DIRECTORY exists.
  git clone git@github.com:mesche-lang/compiler compiler
fi

# Build the compiler
pushd ./compiler
./build.sh
popd

# Build the CLI
source_files=(
    "main.c"
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
        $CC -c "$SOURCE_DIR/$i" -o "$OUTPUT_DIR/${i%.c}.o" $CFLAGS
    else
        echo "Skipping $i, it is up to date."
    fi
done

# Build the static library
echo -e "\nCreating Mesche CLI bin/mesche..."
gcc -o bin/mesche "${object_files[@]}" ./compiler/bin/libmesche.a

echo -e "\nDone!\n"
