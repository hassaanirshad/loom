#!/bin/bash

# Must specify the following two variables
# path to the llvm built according to loom instructions
LLVM_HOME=
# path to loom
LOOM_HOME=

if [ ! -d "$LLVM_HOME" ]
then
	echo "ERROR: Must specify valid path to LLVM home for including the headers directory"
	exit 1
fi

if [ ! -d "$LOOM_HOME" ]
then
	echo "ERROR: Must specify valid path to LOOM home to locate LLVMLoom.so"
	exit 1
fi

headersDirectoryPath="${LLVM_HOME}/tools/clang/lib/Headers"
llvmLoomSharedLibraryPath="${LOOM_HOME}/Release/lib/LLVMLoom.so"

if [ ! -d "$headersDirectoryPath" ]
then
        echo "ERROR: Invalid headers path: $headersDirectoryPath"
        exit 1
fi

if [ ! -f "$llvmLoomSharedLibraryPath" ]
then
        echo "ERROR: Invalid loom shared library path: $llvmLoomSharedLibraryPath"
        exit 1
fi

# Set path if not found
if echo "$PATH" | grep -v -q "${LLVM_HOME}/Release/bin"
then
	export PATH="${LLVM_HOME}/Release/bin:$PATH"
fi

clang -I"${headersDirectoryPath}" -emit-llvm -c -g -o test.bc test.c && \
opt -load "${llvmLoomSharedLibraryPath}" -loom -loom-file test.policy test.bc -o test.opt.bc && \
llc -relocation-model=pic test.opt.bc -o test.s && \
gcc  -fPIC -no-pie test.s -g -o test.native && \
./test.native a b c
