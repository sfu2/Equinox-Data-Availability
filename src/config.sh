#!/bin/bash
set -ex

: "${LLVM_BUILD_PATH:?Set LLVM_BUILD_PATH to the LLVM build prefix containing LLVMConfig.cmake}"

rm -r build || true
rm -r build-Release  || true
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++-15 -DCMAKE_C_COMPILER=clang-15 -GNinja \
    -DLLVM_BUILD_PATH="$LLVM_BUILD_PATH" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake -B build-Release -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER=clang++-15 -DCMAKE_C_COMPILER=clang-15 -GNinja \
    -DLLVM_BUILD_PATH="$LLVM_BUILD_PATH" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
