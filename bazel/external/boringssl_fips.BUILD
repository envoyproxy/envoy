load("@rules_cc//cc:cc_library.bzl", "cc_library")

licenses(["notice"])  # Apache 2

# BoringSSL build as described in the Security Policy for BoringCrypto module "update stream":
# https://boringssl.googlesource.com/boringssl/+/refs/heads/main/crypto/fipsmodule/FIPS.md#update-stream

FIPS_GO_VERSION = "go1.24.2"

FIPS_NINJA_VERSION = "1.10.2"

FIPS_CMAKE_VERSION = "cmake version 3.22.1"

# marker for dir
filegroup(
    name = "crypto_marker",
    srcs = ["crypto/crypto.cc"],
)

cc_library(
    name = "crypto",
    srcs = [
        "crypto/libcrypto.a",
    ],
    hdrs = glob(["include/openssl/*.h"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "ssl",
    srcs = [
        "ssl/libssl.a",
    ],
    hdrs = glob(["include/openssl/*.h"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
    deps = [":crypto"],
)

genrule(
    name = "ninja_bin",
    srcs = [
        "@fips_ninja//:all",
        "@fips_ninja//:configure.py",
    ],
    outs = ["ninja"],
    cmd = """
    SRC_DIR=$$(dirname $(location @fips_ninja//:configure.py))
    OUT_FILE=$$(realpath $@)
    PYTHON_BIN=$$(realpath $(PYTHON3))
    export CC=$(CC)
    export CXX=$(CC)
    # bazel doesnt expose CXX so we have to construct it (or use foreign_cc)
    # in this case we hardcode libc++, once we land hermetic llvm,
    # we could make this configurable
    export CXXFLAGS="-stdlib=libc++"
    export LDFLAGS="-stdlib=libc++ -lc++ -lc++abi -lm -pthread"
    cd $$SRC_DIR
    OUTPUT=$$(mktemp)
    if ! $${PYTHON_BIN} ./configure.py --bootstrap --with-python=$${PYTHON_BIN} > $$OUTPUT 2>&1; then
        echo "Build failed:" >&2
        cat $$OUTPUT >&2
        exit 1
    fi
    cp ninja $$OUT_FILE
    """,
    toolchains = [
        "@rules_python//python:current_py_toolchain",
        "@bazel_tools//tools/cpp:current_cc_toolchain",
    ],
    tools = [
        "@bazel_tools//tools/cpp:current_cc_toolchain",
        "@rules_python//python:current_py_toolchain",
    ],
)

# Set up all the paths, flags, and deps, and then call the boringssl build
BUILD_COMMAND = """
# c++
export CC=$(CC)
# bazel doesnt expose CXX so we have to construct it (or use foreign_cc)
if [[ "$$BAZEL_CXXOPTS" == *"-stdlib=libc++"* ]]; then
    export CXXFLAGS="-stdlib=libc++"
    export LDFLAGS="-stdlib=libc++ -lc++ -lc++abi -lm -pthread"
else
    export CXXFLAGS=""
    export LDFLAGS="-lstdc++ -lm -pthread"
fi

# ninja
NINJA_BINDIR=$$(realpath $$(dirname $(location :ninja_bin)))
export PATH="$${NINJA_BINDIR}:$${PATH}"

# cmake
CMAKE_BINDIR=$$(realpath $$(dirname $(location %s//:bin/cmake)))
export PATH="$${CMAKE_BINDIR}:$$PATH"

# go
GO_BINDIR=$$(realpath $$(dirname $(location %s//:bin/go)))
export GOROOT=$$(dirname "$${GO_BINDIR}")
export GOPATH="$${GOROOT}/gopath"
mkdir -p "$$GOPATH"
export PATH="$${GOPATH}/bin:$${GO_BINDIR}:$${PATH}"

# boringssl
BSSL_SRC=$$(realpath $$(dirname $$(dirname $(location crypto_marker))))
export BSSL_SRC

# fips expectations
export EXPECTED_GO_VERSION="%s"
export EXPECTED_NINJA_VERSION="%s"
export EXPECTED_CMAKE_VERSION="%s"

# We might need to make this configurable if it causes issues outside of CI
export NINJA_CORES=$$(nproc)

CRYPTO_OUT="$$(realpath $(location crypto/libcrypto.a))"
SSL_OUT="$$(realpath $(location ssl/libssl.a))"
export CRYPTO_OUT
export SSL_OUT

OUTPUT=$$(mktemp)
if ! $(location @envoy//bazel/external:boringssl_fips.genrule_cmd) > $$OUTPUT 2>&1; then
    echo "Build failed:"
    cat $$OUTPUT >&2
    exit 1
fi
"""

genrule(
    name = "build",
    srcs = glob(["**"]) + ["crypto_marker"],
    outs = [
        "crypto/libcrypto.a",
        "ssl/libssl.a",
    ],
    cmd = select({
        "@platforms//cpu:x86_64": BUILD_COMMAND % (
            "@fips_cmake_linux_x86_64",
            "@fips_go_linux_amd64",
            FIPS_GO_VERSION,
            FIPS_NINJA_VERSION,
            FIPS_CMAKE_VERSION,
        ),
        "@platforms//cpu:aarch64": BUILD_COMMAND % (
            "@fips_cmake_linux_aarch64",
            "@fips_go_linux_arm64",
            FIPS_GO_VERSION,
            FIPS_NINJA_VERSION,
            FIPS_CMAKE_VERSION,
        ),
    }),
    exec_properties = select({
        "@envoy//bazel:engflow_rbe_x86_64": {
            "Pool": "linux_x64_large",
        },
        "@envoy//bazel:engflow_rbe_aarch64": {
            "Pool": "linux_arm64_small",
        },
    }),
    toolchains = ["@bazel_tools//tools/cpp:current_cc_toolchain"],
    tools = [
        ":ninja_bin",
        "@bazel_tools//tools/cpp:current_cc_toolchain",
        "@envoy//bazel/external:boringssl_fips.genrule_cmd",
    ] + select({
        "@platforms//cpu:x86_64": [
            "@fips_cmake_linux_x86_64//:all",
            "@fips_cmake_linux_x86_64//:bin/cmake",
            "@fips_go_linux_amd64//:all",
            "@fips_go_linux_amd64//:bin/go",
        ],
        "@platforms//cpu:aarch64": [
            "@fips_cmake_linux_aarch64//:all",
            "@fips_cmake_linux_aarch64//:bin/cmake",
            "@fips_go_linux_arm64//:all",
            "@fips_go_linux_arm64//:bin/go",
        ],
    }),
)
