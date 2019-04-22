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
    name = "kafka_request_lib",
    srcs = [
        "normal_code.cc",
    ],
    hdrs = [
        "normal_code.h",
    ],
    deps = [
        ":generated_lib",
    ],
)

envoy_cc_library(
    name = "generated_lib",
    srcs = [],
    hdrs = [
        "generated_header.h",
    ],
)

genrule(
    name = "generate_header_genrule",
    srcs = [
    ],
    outs = [
        "generated_header.h",
    ],
    cmd = "./$(location :code_generator_binary) $(location generated_header.h)",
    tools = [
        ":code_generator_binary",
    ],
)

py_binary(
    name = "code_generator_binary",
    srcs = ["code_generator/generator_script.py"],
    data = glob(["code_generator/*.j2"]),
    main = "code_generator/generator_script.py",
    deps = ["@com_github_pallets_jinja//:jinja2"],
)
