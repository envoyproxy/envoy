#!/usr/bin/env bash

set -eo pipefail

# This provides a test of what toolchains are used according to what environment is setup
# and how bazel is invoked.


echo "Testing toolchain detection"
RESULT_NO_ARGS=fail
RESULT_GCC=fail
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
for key in NO_ARGS GCC GCC_ENV CLANG_ENV; do
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
        expected_gcc_build=false
        if [[ "$key" == "GCC" ]]; then
            expected_gcc_build=true
        fi
        if ! grep -qx "GCC build: $expected_gcc_build" <<< "$output"; then
            echo "❌ $key: //bazel:gcc_build should match $expected_gcc_build"
            echo "$output"
            FAILED=1
        else
            echo "✅ $key passed as expected"
        fi
    fi
done

check_resolution() {
    local expected_compiler="$1"
    local expected_origin="$2"
    shift 2
    local output identity compiler identifier executable
    echo "Testing $expected_compiler toolchain resolution"
    if ! output=$(bazel cquery "$@" @rules_cc//cc:current_cc_toolchain \
        --output=starlark \
        --starlark:expr='"Toolchain: %s|%s|%s" % (providers(target)["ToolchainInfo"].cc.compiler, providers(target)["ToolchainInfo"].cc.toolchain_id, providers(target)["ToolchainInfo"].cc.compiler_executable)' \
        2>&1); then
        echo "$output"
        FAILED=1
        return
    fi
    identity=$(grep '^Toolchain: ' <<< "$output" | cut -d ' ' -f 2-) || true
    IFS='|' read -r compiler identifier executable <<< "$identity"
    if [[ "$compiler" != "$expected_compiler" || -z "$identifier" ]]; then
        echo "❌ Expected $expected_compiler toolchain, got: $identity"
        echo "$output"
        FAILED=1
    elif [[ "$expected_origin" == hermetic && "$executable" != external/* ]]; then
        echo "❌ Expected a hermetic compiler path, got: $identity"
        FAILED=1
    elif [[ "$expected_origin" == host &&
            ( "$executable" != /usr/bin/gcc || "$identifier" != envoy-host-gcc-* ) ]]; then
        echo "❌ Expected the explicit host GCC override, got: $identity"
        FAILED=1
    elif [[ "$expected_compiler" == clang && "$executable" != *llvm_toolchain* ]]; then
        echo "❌ Expected the default LLVM repository, got: $identity"
        FAILED=1
    else
        echo "✅ $identity"
    fi
}

check_resolution clang hermetic
check_resolution gcc hermetic --config=gcc

check_host_gcc() (
    local root fixture output version entry name
    root="$PWD"
    fixture="$root/ci/matrix/.host-gcc-test-$$"
    mkdir "$fixture"
    trap 'rm -rf "$fixture"' EXIT
    cp MODULE.bazel "$fixture/"
    # Keep Envoy as the root module: upstream LLVM and GCC extensions require it.
    shopt -s dotglob nullglob
    for entry in "$root"/*; do
        name="${entry##*/}"
        case "$name" in
            .git|ci|MODULE.bazel|MODULE.bazel.lock|user.bazelrc|ci.bazelrc|bazel-*)
                continue
                ;;
        esac
        ln -s "$entry" "$fixture/$name"
    done
    echo 'envoy_gcc_ext.host(path = "/usr")' >> "$fixture/MODULE.bazel"
    version=$(/usr/bin/gcc -dumpfullversion)
    cd "$fixture"

    check_resolution clang hermetic
    check_resolution gcc host --@envoy//bazel:use_gcc
    if ! output=$(bazel run --@envoy//bazel:use_gcc //tools/toolchain:detect 2>&1) ||
        ! grep -qx 'Compiler: gcc' <<< "$output" ||
        ! grep -qx 'Standard Library: libstdc++' <<< "$output" ||
        ! grep -qx 'GCC build: true' <<< "$output"; then
        echo "❌ Host GCC build failed"
        echo "$output"
        FAILED=1
    fi

    sed -i "s/envoy_gcc_ext.host(path = \"\\/usr\")/envoy_gcc_ext.host(path = \"\\/usr\", gcc_version = \"$version\")/" MODULE.bazel
    check_resolution gcc host --@envoy//bazel:use_gcc
    sed -i "s/gcc_version = \"$version\"/gcc_version = \"0.0.0\"/" MODULE.bazel
    if output=$(bazel cquery --@envoy//bazel:use_gcc @envoy_gcc_host//:cc_toolchain 2>&1); then
        echo "❌ Mismatched host gcc_version unexpectedly succeeded"
        FAILED=1
    elif ! grep -q 'does not match the installed GCC version' <<< "$output"; then
        echo "❌ Host gcc_version failed for an unexpected reason"
        echo "$output"
        FAILED=1
    fi

    sed -i '/^envoy_gcc_ext.host(/d; s/gcc_version = "13.4.0"/gcc_version = "14.3.0"/' MODULE.bazel
    check_resolution gcc hermetic --@envoy//bazel:use_gcc
    if ! output=$(bazel run --@envoy//bazel:use_gcc //tools/toolchain:detect 2>&1) ||
        ! grep -qx 'Compiler: gcc' <<< "$output" ||
        ! grep -qx 'Compiler version: 14.3.0' <<< "$output" ||
        ! grep -qx 'GCC build: true' <<< "$output"; then
        echo "❌ Alternate hermetic GCC version build failed"
        echo "$output"
        FAILED=1
    fi
    [[ "$FAILED" -eq 0 ]]
)

if [[ "${TEST_HOST_GCC:-false}" == true ]] && ! check_host_gcc; then
    FAILED=1
fi

if [[ "$FAILED" -ne 0 ]]; then
    echo "Some test configs did not match expectations"
    exit 1
fi
echo "All test configs passed as expected"
