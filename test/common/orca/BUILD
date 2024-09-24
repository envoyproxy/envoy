load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "orca_load_metrics_test",
    srcs = ["orca_load_metrics_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/orca:orca_load_metrics_lib",
        "//source/common/upstream:upstream_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@com_github_cncf_xds//xds/data/orca/v3:pkg_cc_proto",
        "@com_github_fmtlib_fmt//:fmtlib",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_test(
    name = "orca_parser_test",
    srcs = ["orca_parser_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/common:base64_lib",
        "//source/common/orca:orca_parser",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@com_github_cncf_xds//xds/data/orca/v3:pkg_cc_proto",
        "@com_github_fmtlib_fmt//:fmtlib",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)
