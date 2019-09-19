licenses(["notice"])  # Apache 2

# Kafka network filter.
# Public docs: docs/root/configuration/network_filters/kafka_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "abstract_codec_lib",
    srcs = [],
    hdrs = [
        "codec.h",
    ],
    deps = [
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_library(
    name = "kafka_request_codec_lib",
    srcs = ["request_codec.cc"],
    hdrs = [
        "request_codec.h",
    ],
    deps = [
        ":abstract_codec_lib",
        ":kafka_request_parser_lib",
    ],
)

envoy_cc_library(
    name = "kafka_request_parser_lib",
    srcs = [
        "external/kafka_request_resolver.cc",
        "kafka_request_parser.cc",
    ],
    hdrs = [
        "external/requests.h",
        "kafka_request_parser.h",
    ],
    deps = [
        ":kafka_request_lib",
        ":parser_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "kafka_request_lib",
    srcs = [
    ],
    hdrs = [
        "kafka_request.h",
    ],
    deps = [
        ":serialization_lib",
    ],
)

genrule(
    name = "kafka_request_generated_source",
    srcs = [
        "@kafka_source//:request_protocol_files",
    ],
    outs = [
        "external/requests.h",
        "external/kafka_request_resolver.cc",
    ],
    cmd = """
      ./$(location :kafka_protocol_code_generator_bin) request \
        $(location external/requests.h) $(location external/kafka_request_resolver.cc) \
        $(SRCS)
    """,
    tools = [
        ":kafka_protocol_code_generator_bin",
    ],
)

envoy_cc_library(
    name = "kafka_response_codec_lib",
    srcs = ["response_codec.cc"],
    hdrs = [
        "response_codec.h",
    ],
    deps = [
        ":abstract_codec_lib",
        ":kafka_response_parser_lib",
    ],
)

envoy_cc_library(
    name = "kafka_response_parser_lib",
    srcs = [
        "external/kafka_response_resolver.cc",
        "kafka_response_parser.cc",
    ],
    hdrs = [
        "external/responses.h",
        "kafka_response_parser.h",
    ],
    deps = [
        ":kafka_response_lib",
        ":parser_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "kafka_response_lib",
    srcs = [
    ],
    hdrs = [
        "kafka_response.h",
    ],
    deps = [
        ":serialization_lib",
    ],
)

genrule(
    name = "kafka_response_generated_source",
    srcs = [
        "@kafka_source//:response_protocol_files",
    ],
    outs = [
        "external/responses.h",
        "external/kafka_response_resolver.cc",
    ],
    cmd = """
      ./$(location :kafka_protocol_code_generator_bin) response \
        $(location external/responses.h) $(location external/kafka_response_resolver.cc) \
        $(SRCS)
    """,
    tools = [
        ":kafka_protocol_code_generator_bin",
    ],
)

py_binary(
    name = "kafka_protocol_code_generator_bin",
    srcs = ["protocol/launcher.py"],
    data = glob(["protocol/*.j2"]),
    main = "protocol/launcher.py",
    deps = [
        ":kafka_protocol_generator_lib",
        "@com_github_pallets_jinja//:jinja2",
    ],
)

py_library(
    name = "kafka_protocol_generator_lib",
    srcs = ["protocol/generator.py"],
)

envoy_cc_library(
    name = "parser_lib",
    hdrs = ["parser.h"],
    deps = [
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "serialization_lib",
    srcs = [
        "serialization.cc",
    ],
    hdrs = [
        "external/serialization_composite.h",
        "serialization.h",
    ],
    deps = [
        ":kafka_types_lib",
        "//include/envoy/buffer:buffer_interface",
        "//source/common/common:byte_order_lib",
    ],
)

genrule(
    name = "serialization_composite_generated_source",
    srcs = [],
    outs = [
        "external/serialization_composite.h",
    ],
    cmd = """
      ./$(location :serialization_composite_code_generator_bin) \
      $(location external/serialization_composite.h)
    """,
    tools = [
        ":serialization_composite_code_generator_bin",
    ],
)

py_binary(
    name = "serialization_composite_code_generator_bin",
    srcs = ["serialization/launcher.py"],
    data = glob(["serialization/*.j2"]),
    main = "serialization/launcher.py",
    deps = [
        ":serialization_composite_generator_lib",
        "@com_github_pallets_jinja//:jinja2",
    ],
)

py_library(
    name = "serialization_composite_generator_lib",
    srcs = ["serialization/generator.py"],
)

envoy_cc_library(
    name = "kafka_types_lib",
    hdrs = [
        "kafka_types.h",
    ],
    external_deps = ["abseil_optional"],
    deps = [
        "//source/common/common:macros",
    ],
)
