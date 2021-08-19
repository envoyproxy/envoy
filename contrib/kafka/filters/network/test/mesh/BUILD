load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "filter_unit_test",
    srcs = ["filter_unit_test.cc"],
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/filters/network/source/mesh:filter_lib",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_cc_test(
    name = "request_processor_unit_test",
    srcs = ["request_processor_unit_test.cc"],
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/filters/network/source/mesh:request_processor_lib",
    ],
)

envoy_cc_test(
    name = "abstract_command_unit_test",
    srcs = ["abstract_command_unit_test.cc"],
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/filters/network/source/mesh:abstract_command_lib",
    ],
)
