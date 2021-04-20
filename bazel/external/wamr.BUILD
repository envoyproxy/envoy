load("@rules_foreign_cc//tools/build_defs:cmake.bzl", "cmake_external")

licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

filegroup(
    name = "srcs",
    srcs = glob(["**"]),
    visibility = ["//visibility:public"],
)

cmake_external(
    name = "libiwasm",
    cache_entries = {
        "WAMR_BUILD_AOT": "0",
        "WAMR_BUILD_SIMD": "0",
        "WAMR_BUILD_MULTI_MODULE": "1",
        "WAMR_BUILD_LIBC_WASI": "0",
    },
    lib_source = ":srcs",
    static_libraries = ["libvmlib.a"],
)
