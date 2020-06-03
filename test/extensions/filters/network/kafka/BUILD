load("@rules_python//python:defs.bzl", "py_binary")
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
    name = "buffer_based_test_lib",
    srcs = [],
    hdrs = ["buffer_based_test.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/network/kafka:serialization_lib",
    ],
)

envoy_cc_test_library(
    name = "serialization_utilities_lib",
    srcs = ["serialization_utilities.cc"],
    hdrs = ["serialization_utilities.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/network/kafka:serialization_lib",
    ],
)

envoy_extension_cc_test(
    name = "serialization_test",
    srcs = ["serialization_test.cc"],
    extension_name = "envoy.filters.network.kafka_broker",
    deps = [
        ":serialization_utilities_lib",
        "//source/extensions/filters/network/kafka:serialization_lib",
        "//source/extensions/filters/network/kafka:tagged_fields_lib",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "serialization_composite_test",
    srcs = ["external/serialization_composite_test.cc"],
    extension_name = "envoy.filters.network.kafka_broker",
    deps = [
        ":serialization_utilities_lib",
        "//source/extensions/filters/network/kafka:serialization_lib",
        "//test/mocks/server:server_mocks",
    ],
)

genrule(
    name = "serialization_composite_generated_tests",
    srcs = [],
    outs = ["external/serialization_composite_test.cc"],
    cmd = """
        ./$(location :serialization_composite_test_generator_bin) \
        $(location external/serialization_composite_test.cc)
    """,
    tools = [
        ":serialization_composite_test_generator_bin",
    ],
)

py_binary(
    name = "serialization_composite_test_generator_bin",
    srcs = ["serialization/launcher.py"],
    data = glob(["serialization/*.j2"]),
    main = "serialization/launcher.py",
    deps = [
        "//source/extensions/filters/network/kafka:serialization_composite_generator_lib",
        "@com_github_pallets_jinja//:jinja2",
    ],
)

envoy_cc_test_library(
    name = "message_utilities",
    srcs = [
        "external/request_utilities.cc",
        "external/response_utilities.cc",
    ],
    hdrs = ["message_utilities.h"],
    deps = [
        "//source/extensions/filters/network/kafka:kafka_request_parser_lib",
        "//source/extensions/filters/network/kafka:kafka_response_parser_lib",
    ],
)

envoy_extension_cc_test(
    name = "kafka_request_parser_test",
    srcs = ["kafka_request_parser_test.cc"],
    extension_name = "envoy.filters.network.kafka_broker",
    deps = [
        ":buffer_based_test_lib",
        ":serialization_utilities_lib",
        "//source/extensions/filters/network/kafka:kafka_request_parser_lib",
    ],
)

envoy_extension_cc_test(
    name = "request_codec_unit_test",
    srcs = ["request_codec_unit_test.cc"],
    extension_name = "envoy.filters.network.kafka_broker",
    deps = [
        ":buffer_based_test_lib",
        "//source/extensions/filters/network/kafka:kafka_request_codec_lib",
    ],
)

envoy_extension_cc_test(
    name = "request_codec_integration_test",
    srcs = ["request_codec_integration_test.cc"],
    extension_name = "envoy.filters.network.kafka_broker",
    deps = [
        ":buffer_based_test_lib",
        ":serialization_utilities_lib",
        "//source/extensions/filters/network/kafka:kafka_request_codec_lib",
    ],
)

envoy_extension_cc_test(
    name = "request_codec_request_test",
    srcs = ["external/request_codec_request_test.cc"],
    extension_name = "envoy.filters.network.kafka_broker",
    deps = [
        ":buffer_based_test_lib",
        ":serialization_utilities_lib",
        "//source/extensions/filters/network/kafka:kafka_request_codec_lib",
    ],
)

envoy_extension_cc_test(
    name = "requests_test",
    srcs = ["external/requests_test.cc"],
    extension_name = "envoy.filters.network.kafka_broker",
    deps = [
        ":buffer_based_test_lib",
        "//source/extensions/filters/network/kafka:kafka_request_codec_lib",
    ],
)

genrule(
    name = "request_generated_tests",
    srcs = [
        "@kafka_source//:request_protocol_files",
    ],
    outs = [
        "external/requests_test.cc",
        "external/request_codec_request_test.cc",
        "external/request_utilities.cc",
    ],
    cmd = """
        ./$(location :kafka_protocol_test_generator_bin) request \
        $(location external/requests_test.cc) \
        $(location external/request_codec_request_test.cc) \
        $(location external/request_utilities.cc) \
        $(SRCS)
    """,
    tools = [
        ":kafka_protocol_test_generator_bin",
    ],
)

envoy_extension_cc_test(
    name = "kafka_response_parser_test",
    srcs = ["kafka_response_parser_test.cc"],
    extension_name = "envoy.filters.network.kafka_broker",
    deps = [
        ":buffer_based_test_lib",
        ":serialization_utilities_lib",
        "//source/extensions/filters/network/kafka:kafka_response_parser_lib",
    ],
)

envoy_extension_cc_test(
    name = "response_codec_unit_test",
    srcs = ["response_codec_unit_test.cc"],
    extension_name = "envoy.filters.network.kafka_broker",
    deps = [
        ":buffer_based_test_lib",
        "//source/extensions/filters/network/kafka:kafka_response_codec_lib",
    ],
)

envoy_extension_cc_test(
    name = "response_codec_integration_test",
    srcs = ["response_codec_integration_test.cc"],
    extension_name = "envoy.filters.network.kafka_broker",
    deps = [
        ":buffer_based_test_lib",
        ":serialization_utilities_lib",
        "//source/extensions/filters/network/kafka:kafka_response_codec_lib",
    ],
)

envoy_extension_cc_test(
    name = "response_codec_response_test",
    srcs = ["external/response_codec_response_test.cc"],
    extension_name = "envoy.filters.network.kafka_broker",
    deps = [
        ":buffer_based_test_lib",
        ":serialization_utilities_lib",
        "//source/extensions/filters/network/kafka:kafka_response_codec_lib",
    ],
)

envoy_extension_cc_test(
    name = "responses_test",
    srcs = ["external/responses_test.cc"],
    extension_name = "envoy.filters.network.kafka_broker",
    deps = [
        ":buffer_based_test_lib",
        "//source/extensions/filters/network/kafka:kafka_response_codec_lib",
    ],
)

genrule(
    name = "response_generated_tests",
    srcs = [
        "@kafka_source//:response_protocol_files",
    ],
    outs = [
        "external/responses_test.cc",
        "external/response_codec_response_test.cc",
        "external/response_utilities.cc",
    ],
    cmd = """
        ./$(location :kafka_protocol_test_generator_bin) response \
        $(location external/responses_test.cc) \
        $(location external/response_codec_response_test.cc) \
        $(location external/response_utilities.cc) \
        $(SRCS)
    """,
    tools = [
        ":kafka_protocol_test_generator_bin",
    ],
)

py_binary(
    name = "kafka_protocol_test_generator_bin",
    srcs = ["protocol/launcher.py"],
    data = glob(["protocol/*.j2"]),
    main = "protocol/launcher.py",
    deps = [
        "//source/extensions/filters/network/kafka:kafka_protocol_generator_lib",
        "@com_github_pallets_jinja//:jinja2",
    ],
)

envoy_extension_cc_test(
    name = "metrics_integration_test",
    srcs = ["metrics_integration_test.cc"],
    extension_name = "envoy.filters.network.kafka_broker",
    deps = [
        ":message_utilities",
        "//source/extensions/filters/network/kafka:kafka_broker_filter_lib",
        "//test/common/stats:stat_test_utility_lib",
    ],
)
