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
        "code.cc",
    ],
    hdrs = [
        "header.h",
    ],
)

genrule(
    name = "generate_header_genrule",
    srcs = [
    ],
    outs = [
        "code.cc",
    ],
    cmd = "./$(location :code_generator_binary) $(location code.cc)",
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
