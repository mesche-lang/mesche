#!/usr/bin/env bash

# This script bootstraps the Mesche command line tool into ./tools/mesche so
# that it can be used to build all components in this repo with their
# constituent `project.msc` files.

# Backup the ./tools/mesche folder if it already exists
if [[ -d "./tools/mesche" ]]
then
    if [[ -d "./tools/mesche-old" ]]
    then
        rm -rf ./tools/mesche-old
    fi

    mv ./tools/mesche ./tools/mesche-old
else
    # Ensure the tools folder exists
    mkdir -p ./tools
fi

# Silence pushd and popd
pushd () {
    command pushd "$@" > /dev/null
}

popd () {
    command popd "$@" > /dev/null
}

# Build the compiler and CLI
pushd ./components/compiler && ./build.sh && popd
pushd ./components/cli      && ./build.sh && popd

# Copy the build output to the tools folder
cp -R ./components/cli/bin/boot ./tools/mesche
cp -R ./components/compiler/modules ./tools/mesche
