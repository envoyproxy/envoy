licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "connection_interface",
    hdrs = ["connection.h"],
)

envoy_cc_library(
    name = "context_interface",
    hdrs = ["context.h"],
)

envoy_cc_library(
    name = "context_config_interface",
    hdrs = ["context_config.h"],
)

envoy_cc_library(
    name = "context_manager_interface",
    hdrs = ["context_manager.h"],
    deps = [
        ":context_config_interface",
        ":context_interface",
        "//include/envoy/secret:secret_manager_interface",
        "//include/envoy/stats:stats_interface",
    ],
)
