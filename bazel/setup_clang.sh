#!/usr/bin/env bash

set -e

BAZELRC_FILE="${BAZELRC_FILE:-./clang.bazelrc}"

LLVM_PREFIX=$1

if [[ ! -e "${LLVM_PREFIX}/bin/llvm-config" ]]; then
  echo "Error: cannot find local llvm-config in ${LLVM_PREFIX}."
  exit 1
fi

PATH="$("${LLVM_PREFIX}"/bin/llvm-config --bindir):${PATH}"
export PATH

LLVM_VERSION="$(llvm-config --version)"
LLVM_LIBDIR="$(llvm-config --libdir)"
LLVM_TARGET="$(llvm-config --host-target)"

RT_LIBRARY_PATH="${LLVM_LIBDIR}/clang/${LLVM_VERSION}/lib/${LLVM_TARGET}"

echo "# Generated file, do not edit. If you want to disable clang, just delete this file.
build:clang --action_env='PATH=${PATH}' --host_action_env='PATH=${PATH}'
build:clang --action_env=CC=clang --host_action_env=CC=clang
build:clang --action_env=CXX=clang++ --host_action_env=CXX=clang++
build:clang --action_env='LLVM_CONFIG=${LLVM_PREFIX}/bin/llvm-config' --host_action_env='LLVM_CONFIG=${LLVM_PREFIX}/bin/llvm-config'
build:clang --repo_env='LLVM_CONFIG=${LLVM_PREFIX}/bin/llvm-config'
build:clang --linkopt='-L$(llvm-config --libdir)'
build:clang --linkopt='-Wl,-rpath,$(llvm-config --libdir)'

build:clang-asan --action_env=ENVOY_UBSAN_VPTR=1
build:clang-asan --copt=-fsanitize=vptr,function
build:clang-asan --linkopt=-fsanitize=vptr,function
build:clang-asan --linkopt='-L${RT_LIBRARY_PATH}'
build:clang-asan --linkopt=-l:libclang_rt.ubsan_standalone.a
build:clang-asan --linkopt=-l:libclang_rt.ubsan_standalone_cxx.a
" >"${BAZELRC_FILE}"
