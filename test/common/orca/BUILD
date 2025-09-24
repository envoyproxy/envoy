load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "orca_load_metrics_test",
    srcs = ["orca_load_metrics_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/orca:orca_load_metrics_lib",
        "//source/common/upstream:upstream_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@com_github_cncf_xds//xds/data/orca/v3:pkg_cc_proto",
        "@com_github_fmtlib_fmt//:fmt",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_test(
    name = "orca_parser_test",
    srcs = ["orca_parser_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/common:base64_lib",
        "//source/common/orca:orca_parser",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@com_github_cncf_xds//xds/data/orca/v3:pkg_cc_proto",
        "@com_github_fmtlib_fmt//:fmt",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

envoy_proto_library(
    name = "orca_parser_fuzz_proto",
    srcs = ["orca_parser_fuzz.proto"],
    deps = [
        "//test/fuzz:common_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/client_side_weighted_round_robin/v3:pkg",
    ],
)

envoy_cc_fuzz_test(
    name = "orca_parser_fuzz_test",
    srcs = ["orca_parser_fuzz_test.cc"],
    corpus = "orca_parser_corpus",
    rbe_pool = "6gig",
    deps = [
        ":orca_parser_fuzz_proto_cc_proto",
        "//source/common/common:base64_lib",
        "//source/common/orca:orca_parser",
        "//test/fuzz:utility_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@com_github_cncf_xds//xds/data/orca/v3:pkg_cc_proto",
        "@com_github_fmtlib_fmt//:fmt",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)
