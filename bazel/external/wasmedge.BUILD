load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")

licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

filegroup(
    name = "srcs",
    srcs = glob(["**"]),
)

cmake(
    name = "wasmedge_lib",
    cache_entries = {
        "WASMEDGE_BUILD_AOT_RUNTIME": "Off",
        "WASMEDGE_BUILD_SHARED_LIB": "Off",
        "WASMEDGE_BUILD_STATIC_LIB": "On",
        "WASMEDGE_BUILD_TOOLS": "Off",
        "WASMEDGE_FORCE_DISABLE_LTO": "On",
    },
    generate_args = ["-GNinja"],
    lib_source = ":srcs",
    out_static_libs = ["libwasmedge.a"],
)
