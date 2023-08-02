load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "matchers",
    hdrs = ["matchers.h"],
    deps = ["//test/mocks/network:network_mocks"],
)

envoy_extension_cc_test(
    name = "envoy_deterministic_connection_id_generator_test",
    srcs = ["envoy_deterministic_connection_id_generator_test.cc"],
    extension_names = ["envoy.quic.deterministic_connection_id_generator"],
    tags = ["nofips"],
    deps = [
        ":matchers",
        "//source/extensions/quic/connection_id_generator:envoy_deterministic_connection_id_generator_lib",
        "@com_github_google_quiche//:quic_test_tools_test_utils_lib",
    ],
)
