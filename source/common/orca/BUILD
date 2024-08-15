load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "orca_parser",
    srcs = ["orca_parser.cc"],
    hdrs = ["orca_parser.h"],
    external_deps = [
        "abseil_strings",
        "abseil_statusor",
        "fmtlib",
    ],
    deps = [
        "//envoy/http:header_map_interface",
        "//source/common/common:base64_lib",
        "@com_github_cncf_xds//xds/data/orca/v3:pkg_cc_proto",
    ],
)
