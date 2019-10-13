licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "private_key_interface",
    hdrs = ["private_key.h"],
    external_deps = ["ssl"],
    deps = [
        ":private_key_callbacks_interface",
        "//include/envoy/event:dispatcher_interface",
        "@envoy_api//envoy/api/v2/auth:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "private_key_config_interface",
    hdrs = ["private_key_config.h"],
    deps = [
        ":private_key_interface",
        "//include/envoy/registry",
    ],
)

envoy_cc_library(
    name = "private_key_callbacks_interface",
    hdrs = ["private_key_callbacks.h"],
    external_deps = ["ssl"],
)
