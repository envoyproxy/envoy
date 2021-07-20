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
    name = "filter_unit_test",
    srcs = ["filter_unit_test.cc"],
    # This name needs to be changed after we have the mesh filter ready.
    extension_names = ["envoy.filters.network.kafka_broker"],
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/filters/network/kafka/mesh:filter_lib",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_extension_cc_test(
    name = "request_processor_unit_test",
    srcs = ["request_processor_unit_test.cc"],
    # This name needs to be changed after we have the mesh filter ready.
    extension_names = ["envoy.filters.network.kafka_broker"],
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/filters/network/kafka/mesh:request_processor_lib",
    ],
)

envoy_extension_cc_test(
    name = "abstract_command_unit_test",
    srcs = ["abstract_command_unit_test.cc"],
    # This name needs to be changed after we have the mesh filter ready.
    extension_names = ["envoy.filters.network.kafka_broker"],
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/filters/network/kafka/mesh:abstract_command_lib",
    ],
)
