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
    ],
    hdrs = [
        "library_header.h",
    ],
    deps = [
        ":generated_lib",
    ],
)

envoy_cc_library(
    name = "generated_lib",
    srcs = [],
    hdrs = [
        "external/generated.h",
        "normal_header.h",
    ],
)

genrule(
    name = "generate_header_genrule",
    srcs = [
    ],
    outs = [
        "external/generated.h",
    ],
    cmd = "./$(location :code_generator_binary) $(location external/generated.h)",
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
