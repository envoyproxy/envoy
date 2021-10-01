load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_proto_library(
    name = "predicate",
    srcs = ["predicate.proto"],
)

envoy_cc_extension(
    name = "network_configuration_retry_options_predicate_lib",
    srcs = ["predicate.cc"],
    hdrs = ["predicate.h"],
    repository = "@envoy",
    deps = [
        ":predicate_cc_proto",
        "//library/common/network:configurator_lib",
        "@envoy//envoy/upstream:retry_interface",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":network_configuration_retry_options_predicate_lib",
        ":predicate_cc_proto",
        "@envoy//envoy/upstream:retry_interface",
    ],
)
