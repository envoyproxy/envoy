FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
WORKDIR /workspace
COPY ci/matrix/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
RUN --mount=type=tmpfs,target=/var/cache/apt \
    --mount=type=tmpfs,target=/var/lib/apt/lists \
    apt-get -qq update \
    && apt-get -qq upgrade -y \
    && apt-get -qq install --no-install-recommends -y \
        curl \
        git \
        gosu \
        libc6-dev \
        software-properties-common \
    && curl -fsSL -o /usr/local/bin/bazel https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-amd64 \
    && chmod +x /usr/local/bin/bazel \
    && groupadd -g 1001 envoybuild \
    && useradd -m -u 1001 -g 1001 -s /bin/bash envoybuild \
    && touch /usr/local/bin/test.sh \
    && chmod +x /usr/local/bin/test.sh \
    && cat <<'EOF' > /usr/local/bin/test.sh
#!/usr/bin/env bash
set -eo pipefail
echo "Testing toolchain detection"
RESULT_NO_ARGS=fail
RESULT_GCC=fail
RESULT_CLANG=fail
RESULT_GCC_ENV=fail
RESULT_CLANG_ENV=fail
echo "Testing default build"
if OUTPUT_NO_ARGS=$(bazel run --verbose_failures -s //test:toolchain 2>&1); then
    COMPILER=$(echo "$OUTPUT_NO_ARGS" | grep "Compiler:" | awk '{print $2}' | tr -d '\r\n')
    LIBRARY=$(echo "$OUTPUT_NO_ARGS" | grep "Standard Library:" | awk '{print $3}' | tr -d '\r\n')
    if [[ -n "$COMPILER" && -n "$LIBRARY" ]]; then
        RESULT_NO_ARGS="$COMPILER-$LIBRARY"
    fi
fi
echo "Testing --config=clang-libc++"
if OUTPUT_CLANG=$(bazel run --verbose_failures -s  --config=clang-libc++ //test:toolchain 2>&1); then
    COMPILER=$(echo "$OUTPUT_CLANG" | grep "Compiler:" | awk '{print $2}' | tr -d '\r\n')
    LIBRARY=$(echo "$OUTPUT_CLANG" | grep "Standard Library:" | awk '{print $3}' | tr -d '\r\n')
    if [[ -n "$COMPILER" && -n "$LIBRARY" ]]; then
        RESULT_CLANG="$COMPILER-$LIBRARY"
    fi
fi
echo "Testing --config=gcc"
if OUTPUT_GCC=$(bazel run --verbose_failures -s  --config=gcc //test:toolchain 2>&1); then
    COMPILER=$(echo "$OUTPUT_GCC" | grep "Compiler:" | awk '{print $2}' | tr -d '\r\n')
    LIBRARY=$(echo "$OUTPUT_GCC" | grep "Standard Library:" | awk '{print $3}' | tr -d '\r\n')
    if [[ -n "$COMPILER" && -n "$LIBRARY" ]]; then
        RESULT_GCC="$COMPILER-$LIBRARY"
    fi
fi
echo "Testing CC=gcc"
CC=gcc
CXX=g++
export CC
export CXX
if OUTPUT_GCC_ENV=$(bazel run --verbose_failures -s //test:toolchain 2>&1); then
    COMPILER=$(echo "$OUTPUT_GCC_ENV" | grep "Compiler:" | awk '{print $2}' | tr -d '\r\n')
    LIBRARY=$(echo "$OUTPUT_GCC_ENV" | grep "Standard Library:" | awk '{print $3}' | tr -d '\r\n')
    if [[ -n "$COMPILER" && -n "$LIBRARY" ]]; then
        RESULT_GCC_ENV="$COMPILER-$LIBRARY"
    fi
fi
echo "Testing CC=clang++"
CC=clang
CXX=clang++
export CC
export CXX
if OUTPUT_CLANG_ENV=$(bazel run --verbose_failures -s //test:toolchain 2>&1); then
    COMPILER=$(echo "$OUTPUT_CLANG_ENV" | grep "Compiler:" | awk '{print $2}' | tr -d '\r\n')
    LIBRARY=$(echo "$OUTPUT_CLANG_ENV" | grep "Standard Library:" | awk '{print $3}' | tr -d '\r\n')
    if [[ -n "$COMPILER" && -n "$LIBRARY" ]]; then
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
EOF
ARG MATRIX_SETUP=""
RUN --mount=type=tmpfs,target=/var/cache/apt \
    --mount=type=tmpfs,target=/var/lib/apt/lists \
    TMPFILE=$(mktemp) \
    && echo "$MATRIX_SETUP" > $TMPFILE \
    && chmod +x ${TMPFILE} \
    && ${TMPFILE}
ARG MATRIX_SETUP_EXTRA=""
RUN --mount=type=tmpfs,target=/var/cache/apt \
    --mount=type=tmpfs,target=/var/lib/apt/lists \
    TMPFILE=$(mktemp) \
    && echo "$MATRIX_SETUP_EXTRA" > $TMPFILE \
    && chmod +x ${TMPFILE} \
    && ${TMPFILE}
