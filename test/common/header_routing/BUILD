load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "header_parser_test",
    srcs = ["header_parser_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/header_routing:header_parser_lib",
    ],
)
