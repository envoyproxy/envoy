load("@envoy//bazel/external:aws_lc_build.bzl", "aws_lc_build_command")
load("@rules_cc//cc:defs.bzl", "cc_library")

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
        "@fips_ninja//:all",
        "@fips_ninja//:configure.py",
    ],
    outs = ["ninja"],
    cmd = """
set -eo pipefail
SRC_DIR=$$(dirname $(location @fips_ninja//:configure.py))
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
        "@fips_cmake_src//:all",
        "@fips_cmake_src//:bootstrap",
    ],
    outs = ["cmake"],
    cmd = """
set -eo pipefail
SRC_DIR=$$(dirname $(location @fips_cmake_src//:bootstrap))
MAKE_BIN=$$(dirname $(location @rules_foreign_cc//toolchains/private:make_tool))
export PATH="$${MAKE_BIN}:$$PATH"
CLANG_BIN=$$(dirname $(location @fips_clang_ppc64le//:bin/clang))
export CC="$${CLANG_BIN}/clang"
export CXX="$${CLANG_BIN}/clang++"
cd "$$SRC_DIR"
./bootstrap --parallel=$$(nproc)
make -j$$(nproc)
cp bin/cmake $@
""",
    target_compatible_with = select({
        "@platforms//cpu:x86_64": ["@platforms//:incompatible"],
        "@platforms//cpu:aarch64": ["@platforms//:incompatible"],
        "//conditions:default": [],
    }),
    tools = [
        "@fips_clang_ppc64le//:bin/clang",
        "@fips_clang_ppc64le//:bin/clang++",
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
            "@fips_cmake_linux_x86_64//:all",
            "@fips_cmake_linux_x86_64//:bin/cmake",
            "@fips_go_linux_amd64//:all",
            "@fips_go_linux_amd64//:bin/go",
            "@llvm_toolchain_llvm//:bin/clang",
            "@llvm_toolchain_llvm//:bin/clang++",
        ],
        "@platforms//cpu:aarch64": [
            "@fips_cmake_linux_aarch64//:all",
            "@fips_cmake_linux_aarch64//:bin/cmake",
            "@fips_go_linux_arm64//:all",
            "@fips_go_linux_arm64//:bin/go",
            "@llvm_toolchain_llvm//:bin/clang",
            "@llvm_toolchain_llvm//:bin/clang++",
        ],
        "@platforms//cpu:ppc64le": [
            ":cmake_bin",
            "@fips_clang_ppc64le//:bin/clang",
            "@fips_clang_ppc64le//:bin/clang++",
            "@fips_go_ppc64le//:all",
            "@fips_go_ppc64le//:bin/go",
        ],
        "//conditions:default": [],
    }),
)
