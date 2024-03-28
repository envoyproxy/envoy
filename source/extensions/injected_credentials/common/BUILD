load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "credential_interface",
    hdrs = ["credential.h"],
    deps = [
        "//envoy/http:header_map_interface",
    ],
)

envoy_cc_library(
    name = "secret_reader_lib",
    hdrs = ["secret_reader.h"],
    deps = [
        "//source/common/secret:secret_provider_impl_lib",
    ],
)

envoy_cc_library(
    name = "credential_config_interface",
    hdrs = ["factory.h"],
    deps = [
        ":credential_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/server:factory_context_interface",
    ],
)

envoy_cc_library(
    name = "credential_factory_base_lib",
    hdrs = ["factory_base.h"],
    deps = [
        ":credential_config_interface",
        ":credential_interface",
        "//envoy/server:filter_config_interface",
    ],
)
