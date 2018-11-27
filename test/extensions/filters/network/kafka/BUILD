licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "serialization_test",
    srcs = ["serialization_test.cc"],
    extension_name = "envoy.filters.network.kafka",
    deps = [
        "//source/extensions/filters/network/kafka:serialization_lib",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "serialization_composite_test",
    srcs = ["serialization_composite_test.cc"],
    extension_name = "envoy.filters.network.kafka",
    deps = [
        "//source/extensions/filters/network/kafka:serialization_lib",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "kafka_request_parser_test",
    srcs = ["kafka_request_parser_test.cc"],
    extension_name = "envoy.filters.network.kafka",
    deps = [
        "//source/extensions/filters/network/kafka:kafka_request_lib",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "request_codec_test",
    srcs = ["request_codec_test.cc"],
    extension_name = "envoy.filters.network.kafka",
    deps = [
        "//source/extensions/filters/network/kafka:kafka_request_codec_lib",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "requests_test",
    srcs = ["requests_test.cc"],
    extension_name = "envoy.filters.network.kafka",
    deps = [
        "//source/extensions/filters/network/kafka:kafka_request_codec_lib",
        "//test/mocks/server:server_mocks",
    ],
)

genrule(
    name = "kafka_generated_test",
    srcs = [
        "@kafka_produce_request_spec//file",
        "@kafka_fetch_request_spec//file",
    ],
    outs = ["requests_test.cc"],
    cmd = "./$(location //source/extensions/filters/network/kafka:kafka_code_generator) generate-test $(location requests_test.cc) $(location @kafka_produce_request_spec//file) $(location @kafka_fetch_request_spec//file)",
    tools = [
        "//source/extensions/filters/network/kafka:kafka_code_generator",
    ],
)
