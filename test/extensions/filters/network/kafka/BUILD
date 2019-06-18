licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_cc_test_library(
    name = "serialization_utilities_lib",
    srcs = ["serialization_utilities.cc"],
    hdrs = ["serialization_utilities.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/network/kafka:kafka_request_codec_lib",
        "//source/extensions/filters/network/kafka:serialization_lib",
    ],
)

envoy_extension_cc_test(
    name = "serialization_test",
    srcs = ["serialization_test.cc"],
    extension_name = "envoy.filters.network.kafka",
    deps = [
        ":serialization_utilities_lib",
        "//source/extensions/filters/network/kafka:serialization_lib",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "serialization_composite_test",
    srcs = ["external/serialization_composite_test.cc"],
    extension_name = "envoy.filters.network.kafka",
    deps = [
        ":serialization_utilities_lib",
        "//source/extensions/filters/network/kafka:serialization_lib",
        "//test/mocks/server:server_mocks",
    ],
)

genrule(
    name = "serialization_composite_test_generator",
    srcs = [],
    outs = ["external/serialization_composite_test.cc"],
    cmd = """
        ./$(location //source/extensions/filters/network/kafka:serialization_composite_generator) \
        generate-test $(location external/serialization_composite_test.cc)
    """,
    tools = [
        "//source/extensions/filters/network/kafka:serialization_composite_generator",
    ],
)

envoy_extension_cc_test(
    name = "kafka_request_parser_test",
    srcs = ["kafka_request_parser_test.cc"],
    extension_name = "envoy.filters.network.kafka",
    deps = [
        ":serialization_utilities_lib",
        "//source/extensions/filters/network/kafka:kafka_request_parser_lib",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "request_codec_unit_test",
    srcs = ["request_codec_unit_test.cc"],
    extension_name = "envoy.filters.network.kafka",
    deps = [
        "//source/extensions/filters/network/kafka:kafka_request_codec_lib",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "request_codec_integration_test",
    srcs = ["request_codec_integration_test.cc"],
    extension_name = "envoy.filters.network.kafka",
    deps = [
        ":serialization_utilities_lib",
        "//source/extensions/filters/network/kafka:kafka_request_codec_lib",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "request_codec_request_test",
    srcs = ["external/request_codec_request_test.cc"],
    extension_name = "envoy.filters.network.kafka",
    deps = [
        ":serialization_utilities_lib",
        "//source/extensions/filters/network/kafka:kafka_request_codec_lib",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "requests_test",
    srcs = ["external/requests_test.cc"],
    extension_name = "envoy.filters.network.kafka",
    deps = [
        "//source/extensions/filters/network/kafka:kafka_request_codec_lib",
        "//test/mocks/server:server_mocks",
    ],
)

genrule(
    name = "requests_test_generator",
    srcs = [
        "@kafka_source//:request_protocol_files",
    ],
    outs = [
        "external/requests_test.cc",
        "external/request_codec_request_test.cc",
    ],
    cmd = """
        ./$(location //source/extensions/filters/network/kafka:kafka_code_generator) generate-test \
        $(location external/requests_test.cc) $(location external/request_codec_request_test.cc) \
        $(SRCS)
    """,
    tools = [
        "//source/extensions/filters/network/kafka:kafka_code_generator",
    ],
)
