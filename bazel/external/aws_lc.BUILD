load("@rules_cc//cc:defs.bzl", "cc_library")
load("@envoy//bazel/external:aws_lc_build.bzl", "aws_lc_build_command")

licenses(["notice"])  # Apache 2

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
        "@aws_lc_ninja//:all",
        "@aws_lc_ninja//:configure.py",
    ],
    outs = ["ninja"],
    cmd = """
set -eo pipefail
SRC_DIR=$$(dirname $(location @aws_lc_ninja//:configure.py))
OUT_FILE=$$(realpath $@)
PYTHON_BIN=$$(realpath $(PYTHON3))
cd "$$SRC_DIR"
if ! "$${PYTHON_BIN}" ./configure.py --bootstrap --with-python="$${PYTHON_BIN}" > /tmp/ninja_build.log 2>&1; then
    cat /tmp/ninja_build.log >&2
    exit 1
fi
cp ninja "$$OUT_FILE"
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

genrule(
    name = "cmake_bin",
    srcs = [
        "@aws_lc_cmake_linux_ppc64le//:all",
        "@aws_lc_cmake_linux_ppc64le//:bootstrap",
    ],
    outs = ["cmake"],
    cmd = """
set -eo pipefail
SRC_DIR=$$(dirname $(location @aws_lc_cmake_linux_ppc64le//:bootstrap))
MAKE_BIN=$$(dirname $(location @rules_foreign_cc//toolchains/private:make_tool))
export PATH="$${MAKE_BIN}:$$PATH"
CLANG_BIN=$$(dirname $(location @aws_lc_clang_ppc64le//:bin/clang))
export CC="$${CLANG_BIN}/clang"
export CXX="$${CLANG_BIN}/clang++"
cd "$$SRC_DIR"
./bootstrap --parallel=$$(nproc)
make -j$$(nproc)
cp bin/cmake $@
""",
    tools = [
        "@aws_lc_clang_ppc64le//:bin/clang",
        "@aws_lc_clang_ppc64le//:bin/clang++",
        "@rules_foreign_cc//toolchains/private:make_tool",
    ],
)

genrule(
    name = "build",
    srcs = glob(["**"]),
    outs = [
        "crypto/libcrypto.a",
        "ssl/libssl.a",
    ],
    cmd = select(aws_lc_build_command()),
    tools = [
        ":ninja_bin",
        "@envoy//bazel/external:aws_lc.genrule_cmd",
    ] + select({
        "@platforms//cpu:x86_64": [
            "@aws_lc_clang_x86_64//:bin/clang",
            "@aws_lc_clang_x86_64//:bin/clang++",
            "@aws_lc_go_x86_64//:all",
            "@aws_lc_go_x86_64//:bin/go",
            "@aws_lc_cmake_linux_x86_64//:all",
            "@aws_lc_cmake_linux_x86_64//:bin/cmake",
         ],
        "@platforms//cpu:aarch64": [
            "@aws_lc_clang_aarch64//:bin/clang",
            "@aws_lc_clang_aarch64//:bin/clang++",
            "@aws_lc_go_aarch64//:all",
            "@aws_lc_go_aarch64//:bin/go",
            "@aws_lc_cmake_linux_aarch64//:all",
            "@aws_lc_cmake_linux_aarch64//:bin/cmake",
        ],
        "@platforms//cpu:ppc64le": [
            "@aws_lc_clang_ppc64le//:bin/clang",
            "@aws_lc_clang_ppc64le//:bin/clang++",
            "@aws_lc_go_ppc64le//:all",
            "@aws_lc_go_ppc64le//:bin/go",
            ":cmake_bin",
        ],
        "//conditions:default": [],
    }),
)
