load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "api_versions_unit_test",
    srcs = ["api_versions_unit_test.cc"],
    # This name needs to be changed after we have the mesh filter ready.
    extension_names = ["envoy.filters.network.kafka_broker"],
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/filters/network/kafka/mesh/command_handlers:api_versions_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
    ],
)
