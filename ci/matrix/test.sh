#!/usr/bin/env bash

set -eo pipefail

# This provides a test of what toolchains are used according to what environment is setup
# and how bazel is invoked.


echo "Testing toolchain detection"
RESULT_NO_ARGS=fail
RESULT_GCC=fail
RESULT_CLANG=fail
RESULT_GCC_ENV=fail
RESULT_CLANG_ENV=fail

echo "Testing default build"
if OUTPUT_NO_ARGS=$(bazel run --verbose_failures -s //tools/toolchain:detect 2>&1); then
    COMPILER=$(echo "$OUTPUT_NO_ARGS" | grep "Compiler:" | awk '{print $2}' | tr -d '\r\n')
    LIBRARY=$(echo "$OUTPUT_NO_ARGS" | grep "Standard Library:" | awk '{print $3}' | tr -d '\r\n')
    if [[ -n "$COMPILER" && -n "$LIBRARY" ]]; then
        # shellcheck disable=SC2034
        RESULT_NO_ARGS="$COMPILER-$LIBRARY"
    fi
fi

echo "Testing --config=clang-libc++"
if OUTPUT_CLANG=$(bazel run --verbose_failures -s  --config=clang-libc++ //tools/toolchain:detect 2>&1); then
    COMPILER=$(echo "$OUTPUT_CLANG" | grep "Compiler:" | awk '{print $2}' | tr -d '\r\n')
    LIBRARY=$(echo "$OUTPUT_CLANG" | grep "Standard Library:" | awk '{print $3}' | tr -d '\r\n')
    if [[ -n "$COMPILER" && -n "$LIBRARY" ]]; then
        # shellcheck disable=SC2034
        RESULT_CLANG="$COMPILER-$LIBRARY"
    fi
fi

echo "Testing --config=gcc"
if OUTPUT_GCC=$(bazel run --verbose_failures -s  --config=gcc //tools/toolchain:detect 2>&1); then
    COMPILER=$(echo "$OUTPUT_GCC" | grep "Compiler:" | awk '{print $2}' | tr -d '\r\n')
    LIBRARY=$(echo "$OUTPUT_GCC" | grep "Standard Library:" | awk '{print $3}' | tr -d '\r\n')
    if [[ -n "$COMPILER" && -n "$LIBRARY" ]]; then
        # shellcheck disable=SC2034
        RESULT_GCC="$COMPILER-$LIBRARY"
    fi
fi

echo "Testing CC=gcc"
CC=gcc
CXX=g++
export CC
export CXX
if OUTPUT_GCC_ENV=$(bazel run --verbose_failures -s //tools/toolchain:detect 2>&1); then
    COMPILER=$(echo "$OUTPUT_GCC_ENV" | grep "Compiler:" | awk '{print $2}' | tr -d '\r\n')
    LIBRARY=$(echo "$OUTPUT_GCC_ENV" | grep "Standard Library:" | awk '{print $3}' | tr -d '\r\n')
    if [[ -n "$COMPILER" && -n "$LIBRARY" ]]; then
        # shellcheck disable=SC2034
        RESULT_GCC_ENV="$COMPILER-$LIBRARY"
    fi
fi

echo "Testing CC=clang++"
CC=clang
CXX=clang++
export CC
export CXX
if OUTPUT_CLANG_ENV=$(bazel run --verbose_failures -s //tools/toolchain:detect 2>&1); then
    COMPILER=$(echo "$OUTPUT_CLANG_ENV" | grep "Compiler:" | awk '{print $2}' | tr -d '\r\n')
    LIBRARY=$(echo "$OUTPUT_CLANG_ENV" | grep "Standard Library:" | awk '{print $3}' | tr -d '\r\n')
    if [[ -n "$COMPILER" && -n "$LIBRARY" ]]; then
        # shellcheck disable=SC2034
        RESULT_CLANG_ENV="$COMPILER-$LIBRARY"
    fi
fi

FAILED=0
for key in NO_ARGS GCC CLANG GCC_ENV CLANG_ENV; do
    actual_var="RESULT_${key}"
    expected_var="EXPECTED_${key}"
    output_var="OUTPUT_${key}"
    actual="${!actual_var}"
    expected="${!expected_var}"
    output="${!output_var}"
    if [[ "$actual" != "$expected" ]]; then
        echo "❌ $key: expected=$expected, got=$actual"
        echo "$output"
        FAILED=1
    elif [[ "$actual" == "fail" ]]; then
        echo "✅ $key failed as expected"
    else
        echo "✅ $key passed as expected"
    fi
done
if [[ "$FAILED" -ne 0 ]]; then
    echo "Some test configs did not match expectations"
    exit 1
fi
echo "All test configs passed as expected"
