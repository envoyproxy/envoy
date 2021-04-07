licenses(["notice"])  # Apache 2

load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")

package(default_visibility = ["//visibility:public"])

filegroup(
    name = "srcs",
    srcs = glob(["**"]),
    visibility = ["//visibility:public"],
)

cmake(
    name = "libiwasm",
    cache_entries = {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "On",
        "WAMR_BUILD_AOT": "0",
        "WAMR_BUILD_SIMD": "0",
    },
    lib_source = ":srcs",
    out_shared_libs = ["libiwasm.so"],
    working_directory = "product-mini/platforms/linux"
)
