#!/usr/bin/env bash

set -e

BAZELRC_FILE="${BAZELRC_FILE:-./clang.bazelrc}"

LLVM_PREFIX=$1
LLVM_CONFIG="${LLVM_PREFIX}/bin/llvm-config"

if [[ ! -e "${LLVM_CONFIG}" ]]; then
  echo "Error: cannot find local llvm-config in ${LLVM_PREFIX}."
  exit 1
fi

LLVM_VERSION="$("${LLVM_CONFIG}" --version)"
LLVM_LIBDIR="$("${LLVM_CONFIG}" --libdir)"
LLVM_TARGET="$("${LLVM_CONFIG}" --host-target)"
PATH="$("${LLVM_CONFIG}" --bindir):${PATH}"

RT_LIBRARY_PATH="${LLVM_LIBDIR}/clang/${LLVM_VERSION}/lib/${LLVM_TARGET}"

cat <<EOF > "${BAZELRC_FILE}"
# Generated file, do not edit. If you want to disable clang, just delete this file.
build:clang --host_action_env=PATH=${PATH} --action_env=PATH=${PATH}

build:clang --action_env=LLVM_CONFIG=${LLVM_CONFIG} --host_action_env=LLVM_CONFIG=${LLVM_CONFIG}
build:clang --repo_env=LLVM_CONFIG=${LLVM_CONFIG}
build:clang --linkopt=-L${LLVM_LIBDIR}
build:clang --linkopt=-Wl,-rpath,${LLVM_LIBDIR}

build:clang-asan --linkopt=-L${RT_LIBRARY_PATH}
EOF
