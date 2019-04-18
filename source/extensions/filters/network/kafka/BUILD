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
    name = "kafka_request_codec_lib",
    srcs = ["request_codec.cc"],
    hdrs = [
        "codec.h",
        "request_codec.h",
    ],
    deps = [
        ":kafka_request_lib",
        ":message_lib",
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_library(
    name = "kafka_request_lib",
    srcs = [
        "kafka_request_parser.cc",
        "kafka_request_resolver.cc",
    ],
    hdrs = [
        "kafka_request.h",
        "kafka_request_parser.h",
        "requests.h",
    ],
    deps = [
        ":parser_lib",
        ":serialization_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

genrule(
    name = "kafka_generated_source",
    srcs = [
        "@kafka_source//:request_protocol_files",
    ],
    outs = [
        "requests.h",
        "kafka_request_resolver.cc",
    ],
    cmd = """
      ./$(location :kafka_code_generator) generate-source \
        $(location requests.h) $(location kafka_request_resolver.cc) \
        $(SRCS)
    """,
    tools = [
        ":kafka_code_generator",
    ],
)

py_binary(
    name = "kafka_code_generator",
    srcs = ["protocol_code_generator/kafka_generator.py"],
    data = glob(["protocol_code_generator/*.j2"]),
    main = "protocol_code_generator/kafka_generator.py",
    deps = ["@com_github_pallets_jinja//:jinja2"],
)

envoy_cc_library(
    name = "parser_lib",
    hdrs = ["parser.h"],
    deps = [
        ":kafka_protocol_lib",
        ":message_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "message_lib",
    hdrs = [
        "message.h",
    ],
    deps = [
        "//include/envoy/buffer:buffer_interface",
    ],
)

envoy_cc_library(
    name = "serialization_lib",
    hdrs = [
        "serialization.h",
        "serialization_composite.h",
    ],
    deps = [
        ":kafka_protocol_lib",
        "//include/envoy/buffer:buffer_interface",
        "//source/common/common:byte_order_lib",
    ],
)

genrule(
    name = "serialization_composite_generated_source",
    srcs = [],
    outs = [
        "serialization_composite.h",
    ],
    cmd = """
      ./$(location :serialization_composite_generator) generate-source \
      $(location serialization_composite.h)
    """,
    tools = [
        ":serialization_composite_generator",
    ],
)

py_binary(
    name = "serialization_composite_generator",
    srcs = ["serialization_code_generator/serialization_composite_generator.py"],
    data = glob(["serialization_code_generator/*.j2"]),
    main = "serialization_code_generator/serialization_composite_generator.py",
    deps = ["@com_github_pallets_jinja//:jinja2"],
)

envoy_cc_library(
    name = "kafka_protocol_lib",
    hdrs = [
        "kafka_types.h",
    ],
    external_deps = ["abseil_optional"],
    deps = [
        "//source/common/common:macros",
    ],
)
