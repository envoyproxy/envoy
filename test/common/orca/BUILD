load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "orca_parser_test",
    srcs = ["orca_parser_test.cc"],
    external_deps = [
        "abseil_status",
        "abseil_strings",
        "fmtlib",
    ],
    deps = [
        "//source/common/common:base64_lib",
        "//source/common/orca:orca_parser",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@com_github_cncf_xds//xds/data/orca/v3:pkg_cc_proto",
    ],
)
