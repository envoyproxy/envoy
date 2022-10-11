load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Extensions of QUIC crypto stream.

envoy_extension_package()

envoy_cc_library(
    name = "envoy_deterministic_connection_id_generator_lib",
    srcs = ["envoy_deterministic_connection_id_generator.cc"],
    hdrs = ["envoy_deterministic_connection_id_generator.h"],
    tags = ["nofips"],
    visibility = [
        "//test:__subpackages__",
    ],
    deps = [
        "//source/common/quic:envoy_quic_connection_id_generator_factory_interface",
        "//source/common/quic:envoy_quic_utils_lib",
        "@com_github_google_quiche//:quic_core_deterministic_connection_id_generator_lib",
    ],
)

envoy_cc_extension(
    name = "envoy_deterministic_connection_id_generator_config",
    extra_visibility = [
        "//source/common/quic:__subpackages__",
        "//test:__subpackages__",
    ],
    tags = ["nofips"],
    deps = [
        ":envoy_deterministic_connection_id_generator_lib",
        "//envoy/registry",
        "//source/common/quic:envoy_quic_connection_id_generator_factory_interface",
    ],
)
