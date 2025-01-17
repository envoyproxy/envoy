load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "produce_unit_test",
    srcs = ["produce_unit_test.cc"],
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/filters/network/source/mesh/command_handlers:produce_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
    ],
)

envoy_cc_test(
    name = "produce_record_extractor_unit_test",
    srcs = ["produce_record_extractor_unit_test.cc"],
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/filters/network/source/mesh/command_handlers:produce_record_extractor_lib",
    ],
)

envoy_cc_test(
    name = "fetch_unit_test",
    srcs = ["fetch_unit_test.cc"],
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/filters/network/source/mesh/command_handlers:fetch_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
    ],
)

envoy_cc_test(
    name = "fetch_record_converter_unit_test",
    srcs = ["fetch_record_converter_unit_test.cc"],
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/filters/network/source/mesh/command_handlers:fetch_record_converter_lib",
    ],
)

envoy_cc_test(
    name = "list_offsets_unit_test",
    srcs = ["list_offsets_unit_test.cc"],
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/filters/network/source/mesh/command_handlers:list_offsets_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
    ],
)

envoy_cc_test(
    name = "metadata_unit_test",
    srcs = ["metadata_unit_test.cc"],
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/filters/network/source/mesh/command_handlers:metadata_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
    ],
)

envoy_cc_test(
    name = "api_versions_unit_test",
    srcs = ["api_versions_unit_test.cc"],
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/filters/network/source/mesh/command_handlers:api_versions_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
    ],
)
