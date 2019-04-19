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
