load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cmake",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cmake(
    name = "hyperscan",
    cache_entries = {
        "BUILD_EXAMPLES": "off",
        "CMAKE_CXX_FLAGS": "-lstdc++",
    },
    lib_source = "@io_hyperscan//:all",
    out_static_libs = ["libhs.a"],
    visibility = ["//visibility:private"],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":hyperscan",
        "//envoy/common:regex_interface",
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//contrib/envoy/extensions/hyperscan/v3alpha:pkg_cc_proto",
    ],
)
